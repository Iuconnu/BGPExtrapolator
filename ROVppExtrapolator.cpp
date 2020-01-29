/*************************************************************************
 * This file is part of the BGP Extrapolator.
 *
 * Developed for the SIDR ROV Forecast.
 * This package includes software developed by the SIDR Project
 * (https://sidr.engr.uconn.edu/).
 * See the COPYRIGHT file at the top-level directory of this distribution
 * for details of code ownership.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 ************************************************************************/

#include "ROVppExtrapolator.h"
#include "ROVppASGraph.h"
#include "TableNames.h"


ROVppExtrapolator::ROVppExtrapolator(std::vector<std::string> g,
                                     bool random_b,
                                     std::string r,
                                     std::string e,
                                     std::string f,
                                     uint32_t iteration_size)
    : Extrapolator() {
    // ROVpp specific functions should use the rovpp_graph variable
    // The graph variable maintains backwards compatibility
    it_size = iteration_size;   // Number of prefix to be precessed per iteration (currently not being used)
    random = random_b;          // Flag for using random tiebreaks
    // TODO fix this memory leak
    graph = new ROVppASGraph();
    querier = new ROVppSQLQuerier(g, r, e, f);
    rovpp_graph = dynamic_cast<ROVppASGraph*>(graph);
    rovpp_querier = dynamic_cast<ROVppSQLQuerier*>(querier);
}

ROVppExtrapolator::~ROVppExtrapolator() {}

/** Performs propagation up and down twice. First once with the Victim prefix pairs,
 * then a second time once with the Attacker prefix pairs.
 *
 * Peforms propagation of the victim and attacker prefix pairs one at a time.
 * First victims, and then attackers. The function doesn't use subnet blocks to 
 * iterate over. Instead it does the victims table all at once, then the attackers
 * table all at once.
 *
 * If iteration block sizes need to be considered, then we need to override and use the
 * perform_propagation(bool, size_t) method instead. 
 */
void ROVppExtrapolator::perform_propagation(bool propagate_twice=true) {
    // Main Differences:
    //   No longer need to consider prefix and subnet blocks
    //   No longer printing out ann count, loop counts, tiebreak information, broken path count
    using namespace std;
   
    // Make tmp directory if it does not exist
    DIR* dir = opendir("/dev/shm/bgp");
    if(!dir){
        mkdir("/dev/shm/bgp", 0777); 
    } else {
        closedir(dir);
    }
    // Generate required tables 
    rovpp_querier->clear_results_from_db();
    rovpp_querier->create_results_tbl();
    rovpp_querier->clear_supernodes_from_db();
    rovpp_querier->create_supernodes_tbl();
    rovpp_querier->create_rovpp_blacklist_tbl();
    //rovpp_querier->create_rovpp_blacklist_tbl();
    
    // Generate the graph and populate the stubs & supernode tables
    rovpp_graph->create_graph_from_db(rovpp_querier);
    
    // Main differences start here
    std::cout << "Beginning propagation..." << std::endl;
    
    // Seed MRT announcements and propagate    
    // Iterate over Victim table (first), then Attacker table (second)
    int iter = 0;
    for (const string table_name: {rovpp_querier->victim_table, rovpp_querier->attack_table}) {
        // Get the prefix-origin pairs from the database
        pqxx::result prefix_origin_pairs = rovpp_querier->select_all_pairs_from(table_name);
        // Seed each of the prefix-origin pairs
        for (pqxx::result::const_iterator c = prefix_origin_pairs.begin(); c!=prefix_origin_pairs.end(); ++c) {
            // Extract Arguments needed for give_ann_to_as_path
            std::vector<uint32_t>* parsed_path = parse_path(c["as_path"].as<string>());
            Prefix<> the_prefix = Prefix<>(c["prefix_host"].as<string>(), c["prefix_netmask"].as<string>());
            int64_t timestamp = 1;  // Bogus value just to satisfy function arguments (not actually being used)
            bool is_hijack = table_name == rovpp_querier->attack_table;
            if (is_hijack) {
                // Add origin to attackers
                rovpp_graph->attackers->insert(parsed_path->at(0));
            }
            
            // Seed the announcement
            give_ann_to_as_path(parsed_path, the_prefix, timestamp, is_hijack);
    
            // Clean up
            delete parsed_path;
        }
        
        // This block runs only if we want to propogate up and down twice
        // The similar code block below is mutually exclusive with this code block 
        if (propagate_twice) {
            propagate_up();
            propagate_down();
        }
    }
    
    // This code block runs if we want to propogate up and down only once
    // The similar code block above is mutually exclusive with this code block
    if (!propagate_twice) {
        propagate_up();
        propagate_down();
    }

    for (auto &as : *rovpp_graph->ases){
        // Check for loops
        for (auto it = as.second->loc_rib->begin(); it != as.second->loc_rib->end();) {
            if (it->second.alt != 0 && loop_check(it->second.prefix, as.second->asn, as.second->asn, 0)) {
                it = as.second->loc_rib->erase(it);
            } else {
                ++it;
            }
        }
    }
    
    std::ofstream gvpythonfile;
    gvpythonfile.open("asgraph.py");
    std::vector<uint32_t> to_graph = {  };
    rovpp_graph->to_graphviz(gvpythonfile, to_graph);
    gvpythonfile.close();
    save_results(iter);
    std::cout << "completed: ";
}

/** Seed announcement to all ASes on as_path.
 *
 * The from_monitor attribute is set to true on these announcements so they are
 * not replaced later during propagation. The ROVpp version overrides the origin
 * ASN variable at the origin AS with a flagged value for analysis.
 *
 * @param as_path Vector of ASNs for this announcement.
 * @param prefix The prefix this announcement is for.
 */
void ROVppExtrapolator::give_ann_to_as_path(std::vector<uint32_t>* as_path, 
                                            Prefix<> prefix, 
                                            int64_t timestamp, 
                                            bool hijack) {
    // Handle empty as_path
    if (as_path->empty()) { 
        return;
    }
    
    uint32_t i = 0;
    uint32_t path_l = as_path->size();
    uint32_t origin_asn = as_path->back();
    
    // Announcement at origin for checking along the path
    Announcement ann_to_check_for(origin_asn,
                                  prefix.addr,
                                  prefix.netmask,
                                  0,
                                  timestamp); 
    
    // Full path pointer
    // TODO only handles seeding announcements at origin
    std::vector<uint32_t> cur_path;
    cur_path.push_back(origin_asn);
  
    // Iterate through path starting at the origin
    for (auto it = as_path->rbegin(); it != as_path->rend(); ++it) {
        // Increments path length, including prepending
        i++;
        // If ASN not in graph, continue
        if (graph->ases->find(*it) == graph->ases->end()) {
            continue;
        }
        // Translate ASN to it's supernode
        uint32_t asn_on_path = graph->translate_asn(*it);
        // Find the current AS on the path
        AS *as_on_path = graph->ases->find(asn_on_path)->second;
        // Check if already received this prefix
        if (as_on_path->already_received(ann_to_check_for)) {
            // Find the already received announcement and delete it
            as_on_path->delete_ann(ann_to_check_for);
        }
        
        // If ASes in the path aren't neighbors (data is out of sync)
        bool broken_path = false;

        // It is 3 by default. It stays as 3 if it's the origin.
        int received_from = 300;
        // If this is not the origin AS
        if (i > 1) {
            // Get the previous ASes relationship to current AS
            if (as_on_path->providers->find(*(it - 1)) != as_on_path->providers->end()) {
                received_from = AS_REL_PROVIDER;
            } else if (as_on_path->peers->find(*(it - 1)) != as_on_path->peers->end()) {
                received_from = AS_REL_PEER;
            } else if (as_on_path->customers->find(*(it - 1)) != as_on_path->customers->end()) {
                received_from = AS_REL_CUSTOMER;
            } else {
                broken_path = true;
            }
        }

        // This is how priority is calculated
        uint32_t path_len_weighted = 100 - (i - 1);
        uint32_t priority = received_from + path_len_weighted;
       
        uint32_t received_from_asn = 0;
        
        // TODO Implimentation for rovpp
        // ROV++ Handle origin received_from here
        // BHOLED = 64512
        // HIJACKED = 64513
        // NOTHIJACKED = 64514
        // PREVENTATIVEHIJACKED = 64515
        // PREVENTATIVENOTHIJACKED = 64516
        
        // If this AS is the origin
        if (it == as_path->rbegin()){
            // ROVpp: Set the recv_from to proper flag
            if (hijack) {
                received_from_asn = 64513;
            } else {
                received_from_asn = 64514;
            }
        } else {
            // Otherwise received it from previous AS
            received_from_asn = *(it-1);
        }
        // No break in path so send the announcement
        if (!broken_path) {
            ROVppAnnouncement ann = ROVppAnnouncement(*as_path->rbegin(),
                                            prefix.addr,
                                            prefix.netmask,
                                            priority,
                                            received_from_asn,
                                            timestamp,
                                            0, // policy defaults to BGP
                                            cur_path,
                                            true);
            // Send the announcement to the current AS
            as_on_path->process_announcement(ann, random);
            if (graph->inverse_results != NULL) {
                auto set = graph->inverse_results->find(
                        std::pair<Prefix<>,uint32_t>(ann.prefix, ann.origin));
                // Remove the AS from the prefix's inverse results
                if (set != graph->inverse_results->end()) {
                    set->second->erase(as_on_path->asn);
                }
            }
        } else {
            // Report the broken path if desired
        }
    }
}

/** Withdraw given announcement at given neighbor.
 *
 * @param asn The AS issuing the withdrawal
 * @param ann The announcement to withdraw
 * @param neighbor The AS applying the withdraw
 */
void ROVppExtrapolator::process_withdrawal(uint32_t asn, Announcement ann, ROVppAS *neighbor) {
    // Get the neighbors announcement
    auto neighbor_ann = neighbor->loc_rib->find(ann.prefix);
    
    // If neighbors announcement came from previous AS (relevant withdrawal)
    if (neighbor_ann != neighbor->loc_rib->end() && 
        neighbor_ann->second.received_from_asn == asn) {
        // Add withdrawal to this neighbor
        neighbor->withdraw(ann);
        // Apply withdrawal by deleting ann
        neighbor->loc_rib->erase(neighbor_ann);
        // Recursively process at this neighbor
        process_withdrawals(neighbor);
    }
}

/** Handles processing all withdrawals at a particular AS. 
 *
 * @param as The AS that is sending out it's withdrawals
 */
void ROVppExtrapolator::process_withdrawals(ROVppAS *as) {
    std::vector<std::set<uint32_t>*> neighbor_set;
    neighbor_set.push_back(as->providers);
    neighbor_set.push_back(as->peers);
    neighbor_set.push_back(as->customers);

    // For each withdrawal
    for (auto withdrawal: *as->withdrawals) { 
        // For the current set
        for (auto cur_neighbors: neighbor_set) { 
            // For the current neighbor
            for (uint32_t neighbor_asn : *cur_neighbors) {
                // Get the neighbor
                AS *neighbor = graph->ases->find(neighbor_asn)->second;
                ROVppAS *r_neighbor = dynamic_cast<ROVppAS*>(neighbor);
                // Recursively process withdrawal at neighbor
                process_withdrawal(as->asn, withdrawal, r_neighbor);
           }
        }
    }
}

/** Propagate announcements from customers to peers and providers ASes.
 */
void ROVppExtrapolator::propagate_up() {
    size_t levels = graph->ases_by_rank->size();
    // Propagate to providers
    for (size_t level = 0; level < levels; level++) {
        for (uint32_t asn : *graph->ases_by_rank->at(level)) {
            auto search = graph->ases->find(asn);
            search->second->process_announcements(random);
            ROVppAS *rovpp_as = dynamic_cast<ROVppAS*>(search->second);
            process_withdrawals(rovpp_as);
            send_all_announcements(asn, true, false, false);
        }
    }
    // Propagate to peers
    for (size_t level = 0; level < levels; level++) {
        for (uint32_t asn : *graph->ases_by_rank->at(level)) {
            auto search = graph->ases->find(asn);
            search->second->process_announcements(random);
            ROVppAS *rovpp_as = dynamic_cast<ROVppAS*>(search->second);
            process_withdrawals(rovpp_as);
            send_all_announcements(asn, false, true, false);
        }
    }
}

/** Send "best" announces from providers to customer ASes. 
 */
void ROVppExtrapolator::propagate_down() {
    size_t levels = graph->ases_by_rank->size();
    for (size_t level = levels-1; level-- > 0;) {
        for (uint32_t asn : *graph->ases_by_rank->at(level)) {
            auto search = graph->ases->find(asn);
            search->second->process_announcements(random);
            ROVppAS *rovpp_as = dynamic_cast<ROVppAS*>(search->second);
            process_withdrawals(rovpp_as);
            send_all_announcements(asn, false, false, true);
        }
    }
}

/** Send all announcements kept by an AS to its neighbors. 
 *
 * This approximates the Adj-RIBs-out. ROVpp version simply replaces Announcement 
 * objects with ROVppAnnouncements.
 *
 * @param asn AS that is sending out announces
 * @param to_providers Send to providers
 * @param to_peers Send to peers
 * @param to_customers Send to customers
 */
void ROVppExtrapolator::send_all_announcements(uint32_t asn, 
                                               bool to_providers, 
                                               bool to_peers, 
                                               bool to_customers) {
    // TODO cleanup
    std::vector<Announcement> anns_to_providers;
    std::vector<Announcement> anns_to_peers;
    std::vector<Announcement> anns_to_customers;
    std::vector<std::vector<Announcement>*> outgoing;
    outgoing.push_back(&anns_to_providers);
    outgoing.push_back(&anns_to_peers);
    outgoing.push_back(&anns_to_customers);
    
    // Get the AS that is sending it's announcements
    auto *source_as = graph->ases->find(asn)->second; 
    ROVppAS *rovpp_as = dynamic_cast<ROVppAS*>(source_as);

    for (auto &ann : *source_as->loc_rib) {
        // ROV++ 0.1 do not forward blackhole announcements
        if (rovpp_as != NULL && 
            ann.second.origin == 64512 && 
            rovpp_as->policy_vector.size() > 0 &&
            rovpp_as->policy_vector.at(0) == ROVPPAS_TYPE_ROVPP) {
            continue;
        }
        // Compute portion of priority from path length
        uint32_t old_priority = ann.second.priority;
        uint32_t path_len_weight = old_priority % 100;
        if (path_len_weight == 0) {
            // For MRT ann at origin: old_priority = 400
            path_len_weight = 99;
        } else {
            // Sub 1 for the current hop
            path_len_weight -= 1;
        }

        // Full path generation
        auto cur_path = ann.second.as_path;
        // Handles appending after origin
        if (cur_path.size() == 0 || cur_path.back() != asn) {
            cur_path.push_back(asn);
        }

        // Copy announcement
        Announcement copy = ann.second;
        copy.received_from_asn = asn;
        copy.from_monitor = false;
        copy.as_path = cur_path;
        copy.tiebreak_override = (ann.second.tiebreak_override == 0 ? 0 : asn);

        // Do not propagate any announcements from peers/providers
        // Priority is reduced by 1 per path length
        // Ignore announcements not from a customer
        if (to_providers && ann.second.priority >= 200) {
            
            // Set the priority of the announcement at destination 
            // Base priority is 200 for customer to provider
            
            uint32_t priority = 200 + path_len_weight;
            auto newcopy = copy;
            newcopy.priority = priority;
            anns_to_providers.push_back(newcopy);
        }
        if (to_peers && ann.second.priority >= 200) {
            // Base priority is 100 for peers to peers
            uint32_t priority = 100 + path_len_weight;
            auto newcopy = copy;
            newcopy.priority = priority;
            anns_to_peers.push_back(newcopy);
        }
        if (to_customers) {
            // Base priority is 0 for provider to customers
            uint32_t priority = path_len_weight;
            auto newcopy = copy;
            newcopy.priority = priority;
            anns_to_customers.push_back(newcopy);
        }
            
    }
    // trim provider and peer vectors of preventive and blackhole anns for 0.3 and 0.2bis
    if (rovpp_as != NULL &&
        rovpp_as->policy_vector.size() > 0 &&
        (rovpp_as->policy_vector.at(0) == ROVPPAS_TYPE_ROVPPBP ||
        rovpp_as->policy_vector.at(0) == ROVPPAS_TYPE_ROVPPBIS)) {
        for (auto ann_pair : *rovpp_as->preventive_anns) {
            for (auto it = anns_to_providers.begin(); it != anns_to_providers.end();) {
                if (ann_pair.first.prefix == it->prefix &&
                    ann_pair.first.origin == it->origin) {
                    it = anns_to_providers.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto it = anns_to_peers.begin(); it != anns_to_peers.end();) {
                if (ann_pair.first.prefix == it->prefix &&
                    ann_pair.first.origin == it->origin) {
                    it = anns_to_peers.erase(it);
                } else {
                    ++it;
                }
            }
        }
        for (auto blackhole_ann : *rovpp_as->blackholes) {
            for (auto it = anns_to_providers.begin(); it != anns_to_providers.end();) {
                if (blackhole_ann.prefix == it->prefix &&
                    blackhole_ann.origin == it->origin) {
                    it = anns_to_providers.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto it = anns_to_peers.begin(); it != anns_to_peers.end();) {
                if (blackhole_ann.prefix == it->prefix &&
                    blackhole_ann.origin == it->origin) {
                    it = anns_to_peers.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
    }

    // Clear withdrawals and re-add withdrawals
    for (auto it = source_as->withdrawals->begin(); it != source_as->withdrawals->end();) {
        if (!it->withdraw) {
            it = source_as->withdrawals->erase(it);
        } else {
            // Prepare withdrawals found in withdrawals
            // Set the priority of the announcement at destination 
            uint32_t old_priority = it->priority;
            uint32_t path_len_weight = old_priority % 100;
            if (path_len_weight == 0) {
                // For MRT ann at origin: old_priority = 400
                path_len_weight = 99;
            } else {
                // Sub 1 for the current hop
                path_len_weight -= 1;
            }
            // Full path generation
            auto cur_path = it->as_path;
            // Handles appending after origin
            if (cur_path.size() == 0 || cur_path.back() != asn) {
                cur_path.push_back(asn);
            }
            // Copy announcement
            Announcement copy = *it;
            copy.received_from_asn = asn;
            copy.from_monitor = false;
            copy.tiebreak_override = (it->tiebreak_override == 0 ? 0 : asn);
            copy.as_path = cur_path;

            // Do not propagate any announcements from peers/providers
            // Priority is reduced by 1 per path length
            // Ignore announcements not from a customer
            if (it->priority >= 200) {
                // Set the priority of the announcement at destination 
                // Base priority is 200 for customer to provider
                uint32_t priority = 200 + path_len_weight;
                auto newcopy = copy;
                newcopy.priority = priority;
                anns_to_providers.push_back(newcopy);
            }
            if (it->priority >= 200) {
                // Base priority is 100 for peers to peers
                uint32_t priority = 100 + path_len_weight;
                auto newcopy = copy;
                newcopy.priority = priority;
                anns_to_peers.push_back(newcopy);
            }
            if (true) {
                // Base priority is 0 for provider to customers
                uint32_t priority = path_len_weight;
                auto newcopy = copy;
                newcopy.priority = priority;
                anns_to_customers.push_back(newcopy);
            }
            it = source_as->withdrawals->erase(it);
            //++it;
        }
    }


    // Send the vectors of assembled announcements
    for (uint32_t provider_asn : *source_as->providers) {
        // For each provider, give the vector of announcements
        auto *recving_as = graph->ases->find(provider_asn)->second;
        // NOTE SENT TO ASN IS NO LONGER SET
        recving_as->receive_announcements(anns_to_providers);
    }
    for (uint32_t peer_asn : *source_as->peers) {
        // For each provider, give the vector of announcements
        auto *recving_as = graph->ases->find(peer_asn)->second;
        recving_as->receive_announcements(anns_to_peers);
    }
    for (uint32_t customer_asn : *source_as->customers) {
        // For each customer, give the vector of announcements
        auto *recving_as = graph->ases->find(customer_asn)->second;
        recving_as->receive_announcements(anns_to_customers);
    }

    // Clear withdrawals except for withdrawals
    for (auto it = source_as->withdrawals->begin(); it != source_as->withdrawals->end();) {
        if (!it->withdraw) {
            it = source_as->withdrawals->erase(it);
        } else {
            ++it;
        }
    }
}

/** Check for a loop in the AS path using traceback.
 *
 * @param  p Prefix to check for
 * @param  a The ASN that, if seen, will mean we have a loop
 * @param  cur_as The current AS we are at in the traceback
 * @param  d The current depth of the search
 * @return true if a loop is detected, else false
 */
bool ROVppExtrapolator::loop_check(Prefix<> p, const AS& cur_as, uint32_t a, int d) {
    if (d > 100) { std::cerr << "Maximum depth exceeded during traceback.\n"; return true; }
    auto ann_pair = cur_as.loc_rib->find(p);
    const Announcement &ann = ann_pair->second;
    // i wonder if a cabinet holding a subwoofer counts as a bass case 
    if (ann.received_from_asn == a) { return true; }
    if (ann.received_from_asn == 64512 ||
        ann.received_from_asn == 64513 ||
        ann.received_from_asn == 64514) {
        return false;
    }
    if (ann_pair == cur_as.loc_rib->end()) { 
        //std::cerr << "AS_PATH not continuous during traceback.\n" << a << p.to_cidr(); 
        return false; 
    }
    auto next_as_pair = rovpp_graph->ases->find(ann.received_from_asn);
    if (next_as_pair == rovpp_graph->ases->end()) { std::cerr << "Traced back announcement to nonexistent AS.\n"; return true; }
    const AS& next_as = *next_as_pair->second;
    return loop_check(p, next_as, a, d+1);
}

/** Saves the results of the extrapolation. ROVpp version uses the ROVppQuerier.
 */
void ROVppExtrapolator::save_results(int iteration) {
    // Setup output file stream
    std::ofstream outfile;
    std::ofstream blackhole_outfile;
    std::string file_name = "/dev/shm/bgp/" + std::to_string(iteration) + ".csv";
    std::string blackhole_file_name = "/dev/shm/bgp/blackholes_table_" + std::to_string(iteration) + ".csv";
    outfile.open(file_name);
    blackhole_outfile.open(blackhole_file_name);
    
    // Iterate over all nodes in graph
    std::cout << "Saving Results From Iteration: " << iteration << std::endl;
    for (auto &as : *rovpp_graph->ases){
        as.second->stream_announcements(outfile);
        // Check if it's a ROVpp node
        if (ROVppAS* rovpp_as = dynamic_cast<ROVppAS*>(as.second)) {
          // It is, so now output the blacklist
          rovpp_as->stream_blackholes(blackhole_outfile);
        }
    }
      
    outfile.close();
    blackhole_outfile.close();
    rovpp_querier->copy_results_to_db(file_name);
    rovpp_querier->copy_blackhole_list_to_db(blackhole_file_name);
    std::remove(file_name.c_str());
    std::remove(blackhole_file_name.c_str());
}