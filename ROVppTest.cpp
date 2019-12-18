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
#include "ROVppAS.h"
#include "ROVppAnnouncement.h"

/** ROVppExtrapolator tests, copied from ExtrapolatorTest.cpp 
 */

bool test_ROVppExtrapolator_constructor() {
    ROVppExtrapolator e = ROVppExtrapolator();
    if (e.graph == NULL) { return false; }
    return true;
}

/** Test the loop detection in input MRT AS paths.
 */
bool test_rovpp_find_loop() {
    ROVppExtrapolator e = ROVppExtrapolator();
    std::vector<uint32_t> *as_path = new std::vector<uint32_t>();
    as_path->push_back(1);
    as_path->push_back(2);
    as_path->push_back(3);
    as_path->push_back(1);
    as_path->push_back(4);
    bool loop = e.find_loop(as_path);
    if (!loop) {
        std::cerr << "Loop detection failed." << std::endl;
        return false;
    }
    
    // Prepending handling check
    std::vector<uint32_t> *as_path_b = new std::vector<uint32_t>();
    as_path_b->push_back(1);
    as_path_b->push_back(2);
    as_path_b->push_back(2);
    as_path_b->push_back(3);
    as_path_b->push_back(4);
    loop = e.find_loop(as_path_b);
    if (loop) {
        std::cerr << "Loop prepending correctness failed." << std::endl;
        return false;
    }
    return true;
}

/** Test seeding the graph with announcements from monitors. 
 *  Horizontal lines are peer relationships, vertical lines are customer-provider
 * 
 *    1
 *    |
 *    2--3
 *   /|   
 *  4 5--6 
 *
 *  The test path vect is [3, 2, 5]. 
 */
bool test_rovpp_give_ann_to_as_path() {
    ROVppExtrapolator e = ROVppExtrapolator();
    e.graph->add_relationship(2, 1, AS_REL_PROVIDER);
    e.graph->add_relationship(1, 2, AS_REL_CUSTOMER);
    e.graph->add_relationship(5, 2, AS_REL_PROVIDER);
    e.graph->add_relationship(2, 5, AS_REL_CUSTOMER);
    e.graph->add_relationship(4, 2, AS_REL_PROVIDER);
    e.graph->add_relationship(2, 4, AS_REL_CUSTOMER);
    e.graph->add_relationship(2, 3, AS_REL_PEER);
    e.graph->add_relationship(3, 2, AS_REL_PEER);
    e.graph->add_relationship(5, 6, AS_REL_PEER);
    e.graph->add_relationship(6, 5, AS_REL_PEER);
    e.graph->decide_ranks();

    std::vector<uint32_t> *as_path = new std::vector<uint32_t>();
    as_path->push_back(3);
    as_path->push_back(2);
    as_path->push_back(5);
    Prefix<> p = Prefix<>("137.99.0.0", "255.255.0.0");
    e.give_ann_to_as_path(as_path, p, 2, 0);

    // Test that monitor annoucements were received
    if(!(e.graph->ases->find(2)->second->all_anns->find(p)->second.from_monitor &&
         e.graph->ases->find(3)->second->all_anns->find(p)->second.from_monitor &&
         e.graph->ases->find(5)->second->all_anns->find(p)->second.from_monitor)) {
        std::cerr << "Monitor flag failed." << std::endl;
        return false;
    }
    
    // Test announcement priority calculation
    if (e.graph->ases->find(3)->second->all_anns->find(p)->second.priority != 198 &&
        e.graph->ases->find(2)->second->all_anns->find(p)->second.priority != 299 &&
        e.graph->ases->find(5)->second->all_anns->find(p)->second.priority != 400) {
        std::cerr << "Priority calculation failed." << std::endl;
        return false;
    }

    // Test that only path received the announcement
    if (!(e.graph->ases->find(1)->second->all_anns->size() == 0 &&
        e.graph->ases->find(2)->second->all_anns->size() == 1 &&
        e.graph->ases->find(3)->second->all_anns->size() == 1 &&
        e.graph->ases->find(4)->second->all_anns->size() == 0 &&
        e.graph->ases->find(5)->second->all_anns->size() == 1 &&
        e.graph->ases->find(6)->second->all_anns->size() == 0)) {
        std::cerr << "MRT overseeding check failed." << std::endl;
        return false;
    }

    // Test timestamp tie breaking
    std::vector<uint32_t> *as_path_b = new std::vector<uint32_t>();
    as_path_b->push_back(1);
    as_path_b->push_back(2);
    as_path_b->push_back(4);
    as_path_b->push_back(4);
    e.give_ann_to_as_path(as_path_b, p, 1, 0);

    if (e.graph->ases->find(2)->second->all_anns->find(p)->second.tstamp != 1) {
        return false;
    }
    
    // Test prepending calculation
    if (e.graph->ases->find(2)->second->all_anns->find(p)->second.priority != 298) {
        std::cout << e.graph->ases->find(2)->second->all_anns->find(p)->second.priority << std::endl;
        return false;
    }

    delete as_path;
    delete as_path_b;
    return true;
}

/** Test propagating up in the following test graph.
 *  Horizontal lines are peer relationships, vertical lines are customer-provider
 * 
 *    1
 *    |
 *    2---3
 *   /|    \
 *  4 5--6  7
 *
 *  Starting propagation at 5, only 4 and 7 should not see the announcement.
 */
bool test_rovpp_propagate_up() {
    ROVppExtrapolator e = ROVppExtrapolator();
    e.graph->add_relationship(2, 1, AS_REL_PROVIDER);
    e.graph->add_relationship(1, 2, AS_REL_CUSTOMER);
    e.graph->add_relationship(5, 2, AS_REL_PROVIDER);
    e.graph->add_relationship(2, 5, AS_REL_CUSTOMER);
    e.graph->add_relationship(4, 2, AS_REL_PROVIDER);
    e.graph->add_relationship(2, 4, AS_REL_CUSTOMER);
    e.graph->add_relationship(7, 3, AS_REL_PROVIDER);
    e.graph->add_relationship(3, 7, AS_REL_CUSTOMER);
    e.graph->add_relationship(2, 3, AS_REL_PEER);
    e.graph->add_relationship(3, 2, AS_REL_PEER);
    e.graph->add_relationship(5, 6, AS_REL_PEER);
    e.graph->add_relationship(6, 5, AS_REL_PEER);

    e.graph->decide_ranks();
    Prefix<> p = Prefix<>("137.99.0.0", "255.255.0.0");
    
    Announcement ann = Announcement(13796, p.addr, p.netmask, 22742);
    ann.from_monitor = true;
    ann.priority = 290;
    e.graph->ases->find(5)->second->process_announcement(ann);
    e.propagate_up();
    
    // Check all announcements are propagted
    if (!(e.graph->ases->find(1)->second->all_anns->size() == 1 &&
          e.graph->ases->find(2)->second->all_anns->size() == 1 &&
          e.graph->ases->find(3)->second->all_anns->size() == 1 &&
          e.graph->ases->find(4)->second->all_anns->size() == 0 &&
          e.graph->ases->find(5)->second->all_anns->size() == 1 &&
          e.graph->ases->find(6)->second->all_anns->size() == 1 &&
          e.graph->ases->find(7)->second->all_anns->size() == 0)) {
        std::cerr << "Loop detection failed." << std::endl;
        return false;
    }
    
    // Check propagation priority calculation
    if (e.graph->ases->find(5)->second->all_anns->find(p)->second.priority != 290 &&
        e.graph->ases->find(2)->second->all_anns->find(p)->second.priority != 289 &&
        e.graph->ases->find(6)->second->all_anns->find(p)->second.priority != 189 &&
        e.graph->ases->find(1)->second->all_anns->find(p)->second.priority != 288 &&
        e.graph->ases->find(3)->second->all_anns->find(p)->second.priority != 188) {
        std::cerr << "Propagted priority calculation failed." << std::endl;
        return false;
    }
    return true;
}

/** Test propagating down in the following test graph.
 *  Horizontal lines are peer relationships, vertical lines are customer-provider
 * 
 *    1
 *    |
 *    2--3
 *   /|   
 *  4 5--6 
 *
 *  Starting propagation at 2, 4 and 5 should see the announcement.
 */
bool test_rovpp_propagate_down() {
    ROVppExtrapolator e = ROVppExtrapolator();
    e.graph->add_relationship(2, 1, AS_REL_PROVIDER);
    e.graph->add_relationship(1, 2, AS_REL_CUSTOMER);
    e.graph->add_relationship(5, 2, AS_REL_PROVIDER);
    e.graph->add_relationship(2, 5, AS_REL_CUSTOMER);
    e.graph->add_relationship(4, 2, AS_REL_PROVIDER);
    e.graph->add_relationship(2, 4, AS_REL_CUSTOMER);
    e.graph->add_relationship(2, 3, AS_REL_PEER);
    e.graph->add_relationship(3, 2, AS_REL_PEER);
    e.graph->add_relationship(5, 6, AS_REL_PEER);
    e.graph->add_relationship(6, 5, AS_REL_PEER);

    e.graph->decide_ranks();
    
    Prefix<> p = Prefix<>("137.99.0.0", "255.255.0.0");
    Announcement ann = Announcement(13796, p.addr, p.netmask, 22742);
    ann.from_monitor = true;
    ann.priority = 290;
    e.graph->ases->find(2)->second->process_announcement(ann);
    e.propagate_down();
    
    // Check all announcements are propagted
    if (!(e.graph->ases->find(1)->second->all_anns->size() == 0 &&
        e.graph->ases->find(2)->second->all_anns->size() == 1 &&
        e.graph->ases->find(3)->second->all_anns->size() == 0 &&
        e.graph->ases->find(4)->second->all_anns->size() == 1 &&
        e.graph->ases->find(5)->second->all_anns->size() == 1 &&
        e.graph->ases->find(6)->second->all_anns->size() == 0)) {
        return false;
    }
    
    if (e.graph->ases->find(2)->second->all_anns->find(p)->second.priority != 290 &&
        e.graph->ases->find(4)->second->all_anns->find(p)->second.priority != 89 &&
        e.graph->ases->find(5)->second->all_anns->find(p)->second.priority != 89) {
        std::cerr << "Propagted priority calculation failed." << std::endl;
        return false;
    }
    return true;
}

/** Test send_all_announcements in the following test graph.
 *  Horizontal lines are peer relationships, vertical lines are customer-provider
 * 
 *    1
 *    |
 *    2---3
 *   /|    \
 *  4 5--6  7
 *
 *  Starting propagation at 5, only 4 and 7 should not see the announcement.
 */
bool test_rovpp_send_all_announcements() {
    ROVppExtrapolator e = ROVppExtrapolator();
    e.graph->add_relationship(2, 1, AS_REL_PROVIDER);
    e.graph->add_relationship(1, 2, AS_REL_CUSTOMER);
    e.graph->add_relationship(5, 2, AS_REL_PROVIDER);
    e.graph->add_relationship(2, 5, AS_REL_CUSTOMER);
    e.graph->add_relationship(4, 2, AS_REL_PROVIDER);
    e.graph->add_relationship(2, 4, AS_REL_CUSTOMER);
    e.graph->add_relationship(7, 3, AS_REL_PROVIDER);
    e.graph->add_relationship(3, 7, AS_REL_CUSTOMER);
    e.graph->add_relationship(2, 3, AS_REL_PEER);
    e.graph->add_relationship(3, 2, AS_REL_PEER);
    e.graph->add_relationship(5, 6, AS_REL_PEER);
    e.graph->add_relationship(6, 5, AS_REL_PEER);

    e.graph->decide_ranks();

    std::vector<uint32_t> *as_path = new std::vector<uint32_t>();
    as_path->push_back(2);
    as_path->push_back(4);
    Prefix<> p = Prefix<>("137.99.0.0", "255.255.0.0");
    e.give_ann_to_as_path(as_path, p, 0, 0);
    delete as_path;

    // Check to providers
    e.send_all_announcements(2, true, false, false);
    if (!(e.graph->ases->find(1)->second->incoming_announcements->size() == 1 &&
          e.graph->ases->find(2)->second->all_anns->size() == 1 &&
          e.graph->ases->find(3)->second->all_anns->size() == 0 &&
          e.graph->ases->find(4)->second->all_anns->size() == 1 &&
          e.graph->ases->find(5)->second->all_anns->size() == 0 &&
          e.graph->ases->find(6)->second->all_anns->size() == 0 &&
          e.graph->ases->find(7)->second->all_anns->size() == 0)) {
        std::cerr << "Err sending to providers" << std::endl;
        return false;
    }
    
    // Check to peers
    e.send_all_announcements(2, false, true, false);
    if (!(e.graph->ases->find(1)->second->incoming_announcements->size() == 1 &&
          e.graph->ases->find(2)->second->all_anns->size() == 1 &&
          e.graph->ases->find(3)->second->incoming_announcements->size() == 1 &&
          e.graph->ases->find(4)->second->all_anns->size() == 1 &&
          e.graph->ases->find(5)->second->incoming_announcements->size() == 0 &&
          e.graph->ases->find(6)->second->all_anns->size() == 0 &&
          e.graph->ases->find(7)->second->all_anns->size() == 0)) {
        std::cerr << "Err sending to peers" << std::endl;
        return false;
    }

    // Check to customers
    e.send_all_announcements(2, false, false, true);
    if (!(e.graph->ases->find(1)->second->incoming_announcements->size() == 1 &&
          e.graph->ases->find(2)->second->all_anns->size() == 1 &&
          e.graph->ases->find(3)->second->incoming_announcements->size() == 1 &&
          e.graph->ases->find(4)->second->all_anns->size() == 1 &&
          e.graph->ases->find(5)->second->incoming_announcements->size() == 1 &&
          e.graph->ases->find(6)->second->all_anns->size() == 0 &&
          e.graph->ases->find(7)->second->all_anns->size() == 0)) {
        std::cerr << "Err sending to customers" << std::endl;
        return false;
    }
    
    // Check priority calculation
    if (e.graph->ases->find(2)->second->all_anns->find(p)->second.priority != 299 &&
        e.graph->ases->find(1)->second->all_anns->find(p)->second.priority != 289 &&
        e.graph->ases->find(3)->second->all_anns->find(p)->second.priority != 189 &&
        e.graph->ases->find(5)->second->all_anns->find(p)->second.priority != 89) {
        std::cerr << "Send all announcement priority calculation failed." << std::endl;
        return false;
    }

    return true;
}

/** Test adding a AS relationship to graph by building this test graph.
 *  AS2 is a provider to AS1, which is a peer to AS3.
 *
 *  AS2
 *   |
 *   V
 *  AS1---AS3
 * 
 * @return true if successful, otherwise false.
 */
bool test_rovpp_add_relationship(){
    ROVppASGraph graph = ROVppASGraph();
    graph.add_relationship(1, 2, AS_REL_PROVIDER);
    graph.add_relationship(2, 1, AS_REL_CUSTOMER);
    graph.add_relationship(1, 3, AS_REL_PEER);
    graph.add_relationship(3, 1, AS_REL_PEER);
    if (*graph.ases->find(1)->second->providers->find(2) != 2) {
        return false;
    }
    if (*graph.ases->find(1)->second->peers->find(3) != 3) {
        return false;
    }
    if (*graph.ases->find(2)->second->customers->find(1) != 1) {
        return false;
    }
    if (*graph.ases->find(3)->second->peers->find(1) != 1) {
        return false;
    }
    return true;
}

/** Test translating a ASN into it's supernode Identifier
 *
 * @return true if successful, otherwise false.
 */
bool test_rovpp_translate_asn(){
    ROVppASGraph graph = ROVppASGraph();
    // add_relation(Target, Neighber, Relation of neighbor to target)
    // Cycle 
    graph.add_relationship(2, 1, AS_REL_PROVIDER);
    graph.add_relationship(1, 2, AS_REL_CUSTOMER);
    graph.add_relationship(1, 3, AS_REL_PROVIDER);
    graph.add_relationship(3, 1, AS_REL_CUSTOMER);
    graph.add_relationship(3, 2, AS_REL_PROVIDER);
    graph.add_relationship(2, 3, AS_REL_CUSTOMER);
    // Customer
    graph.add_relationship(5, 3, AS_REL_PROVIDER);
    graph.add_relationship(3, 5, AS_REL_CUSTOMER);
    graph.add_relationship(6, 3, AS_REL_PROVIDER);
    graph.add_relationship(3, 6, AS_REL_CUSTOMER);
    // Peer
    graph.add_relationship(4, 3, AS_REL_PEER);
    graph.add_relationship(3, 4, AS_REL_PEER);
    graph.tarjan();
    graph.combine_components();

    if (graph.translate_asn(1) != 1 ||
        graph.translate_asn(2) != 1 ||
        graph.translate_asn(3) != 1)
        return false;
    if (graph.translate_asn(4) != 4 ||
        graph.translate_asn(5) != 5 ||
        graph.translate_asn(6) != 6)
        return false;
    return true;
}

/** Test assignment of ranks to each AS in the graph. 
 *  Horizontal lines are peer relationships, vertical lines are customer-provider
 * 
 *    1
 *   / \
 *  2   3--4
 *     / \
 *    5   6
 *
 * @return true if successful, otherwise false.
 */
bool test_rovpp_decide_ranks(){
    ROVppASGraph graph = ROVppASGraph();
    graph.add_relationship(2, 1, AS_REL_PROVIDER);
    graph.add_relationship(1, 2, AS_REL_CUSTOMER);
    graph.add_relationship(3, 1, AS_REL_PROVIDER);
    graph.add_relationship(1, 3, AS_REL_CUSTOMER);
    graph.add_relationship(5, 3, AS_REL_PROVIDER);
    graph.add_relationship(3, 5, AS_REL_CUSTOMER);
    graph.add_relationship(6, 3, AS_REL_PROVIDER);
    graph.add_relationship(3, 6, AS_REL_CUSTOMER);
    graph.add_relationship(4, 3, AS_REL_PEER);
    graph.add_relationship(3, 4, AS_REL_PEER);
    graph.decide_ranks();
    int num_systems = 6;
    int acc = 0;
    for (auto set : *graph.ases_by_rank) {
        acc += set->size();
    }
    if (acc != num_systems) {
        std::cerr << "Number of ASes in ases_by_rank != total number of ASes." << std::endl;
        return false;
    }
    if (graph.ases->find(1)->second->rank == 2 &&
        graph.ases->find(2)->second->rank == 0 &&
        graph.ases->find(3)->second->rank == 1 &&
        graph.ases->find(4)->second->rank == 0 &&
        graph.ases->find(5)->second->rank == 0 &&
        graph.ases->find(6)->second->rank == 0) {
        return true;
    }
    return false;
}

/** Test removing stub ASes from the graph. 
 * 
 *    1
 *   / \
 *  2   3--4
 *     / \
 *    5   6
 *
 * @return true if successful, otherwise false.
 */
bool test_rovpp_remove_stubs(){
    ROVppASGraph graph = ROVppASGraph();
    SQLQuerier *querier = new SQLQuerier("mrt_announcements", "test_results", "test_results", "test_results_d");
    graph.add_relationship(2, 1, AS_REL_PROVIDER);
    graph.add_relationship(1, 2, AS_REL_CUSTOMER);
    graph.add_relationship(3, 1, AS_REL_PROVIDER);
    graph.add_relationship(1, 3, AS_REL_CUSTOMER);
    graph.add_relationship(5, 3, AS_REL_PROVIDER);
    graph.add_relationship(3, 5, AS_REL_CUSTOMER);
    graph.add_relationship(6, 3, AS_REL_PROVIDER);
    graph.add_relationship(3, 6, AS_REL_CUSTOMER);
    graph.add_relationship(4, 3, AS_REL_PEER);
    graph.add_relationship(3, 4, AS_REL_PEER);
    graph.remove_stubs(querier);
    delete querier;
    // Stub removal
    if (graph.ases->find(2) != graph.ases->end() ||
        graph.ases->find(5) != graph.ases->end() ||
        graph.ases->find(6) != graph.ases->end()) {
        std::cerr << "Failed stubs removal check." << std::endl;
        return false;
    }
    // Stub translation
    if (graph.translate_asn(2) != 1 ||
        graph.translate_asn(5) != 3 ||
        graph.translate_asn(6) != 3) {
        std::cerr << "Failed stubs translation check." << std::endl;
        return false;
    }
    return true;
}



/** Test deterministic randomness within the AS scope.
 *
 * @return True if successful, otherwise false
 */
bool test_rovpp_get_random(){
    // Check randomness
    ROVppAS *as_a = new ROVppAS(832);
    ROVppAS *as_b = new ROVppAS(832);
    bool ran_a_1 = as_a->get_random();
    bool ran_a_2 = as_a->get_random();
    bool ran_a_3 = as_a->get_random();
    bool ran_b_1 = as_b->get_random();
    bool ran_b_2 = as_b->get_random();
    bool ran_b_3 = as_b->get_random();
    delete as_a;
    delete as_b;
    if (ran_a_1 != ran_b_1 || ran_a_2 != ran_b_2 || ran_a_3 != ran_b_3) {
        std::cerr << ran_a_1 << " != " << ran_b_1 << std::endl;
        std::cerr << ran_a_2 << " != " << ran_b_2 << std::endl;
        std::cerr << ran_a_3 << " != " << ran_b_3 << std::endl;
        std::cerr << "Failed deterministic randomness check." << std::endl;
        return false;
    }
    return true;
}

/** Test adding neighbor AS to the appropriate set based on the relationship.
 *
 * @return True if successful, otherwise false
 */
bool test_rovpp_add_neighbor(){
    ROVppAS as = ROVppAS();
    as.add_neighbor(1, AS_REL_PROVIDER);
    as.add_neighbor(2, AS_REL_PEER);
    as.add_neighbor(3, AS_REL_CUSTOMER);
    if (as.providers->find(1) == as.providers->end() ||
        as.peers->find(2) == as.peers->end() ||
        as.customers->find(3) == as.customers->end()) {
        std::cerr << "Failed add neighbor check." << std::endl;
        return false;
    }
    return true;
}

/** Test removing neighbor AS from the appropriate set based on the relationship.
 *
 * @return True if successful, otherwise false
 */
bool test_rovpp_remove_neighbor(){
    ROVppAS as = ROVppAS();
    as.add_neighbor(1, AS_REL_PROVIDER);
    as.add_neighbor(2, AS_REL_PEER);
    as.add_neighbor(3, AS_REL_CUSTOMER);
    as.remove_neighbor(1, AS_REL_PROVIDER);
    as.remove_neighbor(2, AS_REL_PEER);
    as.remove_neighbor(3, AS_REL_CUSTOMER);
    if (as.providers->find(1) != as.providers->end() ||
        as.peers->find(2) != as.peers->end() ||
        as.customers->find(3) != as.customers->end()) {
        std::cerr << "Failed remove neighbor check." << std::endl;
        return false;
    }
    return true;
}

/** Test pushing the received announcement to the incoming_announcements vector. 
 *
 * @return true if successful.
 */
bool test_rovpp_receive_announcements(){
    Announcement ann = Announcement(13796, 0x89630000, 0xFFFF0000, 22742);
    std::vector<Announcement> vect = std::vector<Announcement>();
    vect.push_back(ann);
    // this function should make a copy of the announcement
    // if it does not, it is incorrect
    Prefix<> old_prefix = ann.prefix;
    ann.prefix.addr = 0x321C9F00;
    ann.prefix.netmask = 0xFFFFFF00;
    Prefix<> new_prefix = ann.prefix;
    vect.push_back(ann);
    ROVppAS as = ROVppAS();
    as.receive_announcements(vect);
    if (as.incoming_announcements->size() != 2) { return false; }
    // order really doesn't matter here
    for (Announcement a : *as.incoming_announcements) {
        if (a.prefix != old_prefix && a.prefix != new_prefix) {
            return false;
        }
    }
    return true;
}

/** Test directly adding an announcement to the all_anns map.
 *
 * @return true if successful.
 */
bool test_rovpp_process_announcement(){
    Announcement ann = Announcement(13796, 0x89630000, 0xFFFF0000, 22742);
    // this function should make a copy of the announcement
    // if it does not, it is incorrect
    ROVppAS as = ROVppAS();
    as.process_announcement(ann);
    Prefix<> old_prefix = ann.prefix;
    ann.prefix.addr = 0x321C9F00;
    ann.prefix.netmask = 0xFFFFFF00;
    Prefix<> new_prefix = ann.prefix;
    as.process_announcement(ann);
    if (new_prefix != as.all_anns->find(ann.prefix)->second.prefix ||
        old_prefix != as.all_anns->find(old_prefix)->second.prefix) {
        return false;
    }

    // Check priority
    Prefix<> p = Prefix<>("1.1.1.0", "255.255.255.0");
    Announcement a1 = Announcement(111, p.addr, p.netmask, 199, 222, false);
    Announcement a2 = Announcement(111, p.addr, p.netmask, 298, 223, false);
    as.process_announcement(a1);
    as.process_announcement(a2);
    if (as.all_anns->find(p)->second.received_from_asn != 223 ||
        as.depref_anns->find(p)->second.received_from_asn != 222) {
        std::cerr << "Failed best path inference priority check." << std::endl;
        return false;
    }    

    // Check new best announcement
    Announcement a3 = Announcement(111, p.addr, p.netmask, 299, 224, false);
    as.process_announcement(a3);
    if (as.all_anns->find(p)->second.received_from_asn != 224 ||
        as.depref_anns->find(p)->second.received_from_asn != 223) {
        std::cerr << "Failed best path priority correction check." << std::endl;
        return false;
    } 
    return true;
}

/** Test the following properties of the best path selection algorithm.
 *
 * 1. Customer route > peer route > provider route
 * 2. Shortest path takes priority
 * 3. Announcements from monitors are never overwritten
 * 4. Announcements for local prefixes are never overwritten
 *
 * Items one, two, and three are all covered by the priority attribute working correctly. 
 * Item three requires the from_monitor attribute to work. 
 */
bool test_rovpp_process_announcements(){
    Announcement ann1 = Announcement(13796, 0x89630000, 0xFFFF0000, 22742);
    Prefix<> ann1_prefix = ann1.prefix;
    Announcement ann2 = Announcement(13796, 0x321C9F00, 0xFFFFFF00, 22742);
    Prefix<> ann2_prefix = ann2.prefix;
    ROVppAS as = ROVppAS();
    // build a vector of announcements
    std::vector<Announcement> vect = std::vector<Announcement>();
    ann1.priority = 100;
    ann2.priority = 200;
    ann2.from_monitor = true;
    vect.push_back(ann1);
    vect.push_back(ann2);

    // does it work if all_anns is empty?
    as.receive_announcements(vect);
    as.process_announcements();
    if (as.all_anns->find(ann1_prefix)->second.priority != 100) {
        std::cerr << "Failed to add an announcement to an empty map" << std::endl;
        return false;
    }
    
    // higher priority should overwrite lower priority
    vect.clear();
    ann1.priority = 290;
    vect.push_back(ann1);
    as.receive_announcements(vect);
    as.process_announcements();
    if (as.all_anns->find(ann1_prefix)->second.priority != 290) {
        std::cerr << "Higher priority announcements should overwrite lower priority ones." << std::endl;
        return false;
    }
    
    // lower priority should not overwrite higher priority
    vect.clear();
    ann1.priority = 200;
    vect.push_back(ann1);
    as.receive_announcements(vect);
    as.process_announcements();
    if (as.all_anns->find(ann1_prefix)->second.priority != 290) {
        std::cerr << "Lower priority announcements should not overwrite higher priority ones." << std::endl;
        return false;
    }

    // one more test just to be sure
    vect.clear();
    ann1.priority = 299;
    vect.push_back(ann1);
    as.receive_announcements(vect);
    as.process_announcements();
    if (as.all_anns->find(ann1_prefix)->second.priority != 299) {
        std::cerr << "How did you manage to fail here?" << std::endl;
        return false;
    }

    // make sure ann2 doesn't get overwritten, ever, even with higher priority
    vect.clear();
    ann2.priority = 300;
    vect.push_back(ann2);
    as.receive_announcements(vect);
    as.process_announcements();
    if (as.all_anns->find(ann2_prefix)->second.priority != 200) {
        std::cerr << "Announcements from_monitor should not be overwritten." << std::endl;
        return false;
    }

    return true;
}
/** Test clearing all announcements.
 *
 * @return true if successful.
 */
bool test_rovpp_clear_announcements(){
    Announcement ann = Announcement(13796, 0x89630000, 0xFFFF0000, 22742);
    ROVppAS as = ROVppAS();
    // if receive_announcement is broken, this test will also be broken
    as.process_announcement(ann);
    if (as.all_anns->size() != 1) {
        return false;
    }
    as.clear_announcements();
    if (as.all_anns->size() != 0) {
        return false;
    }
    return true;
}

/** Test checking if announcement is already received by an AS.
 *
 * @return true if successful.
 */
bool test_rovpp_already_received(){
    Announcement ann1 = Announcement(13796, 0x89630000, 0xFFFF0000, 22742);
    Announcement ann2 = Announcement(13796, 0x321C9F00, 0xFFFFFF00, 22742);
    ROVppAS as = ROVppAS();
    // if receive_announcement is broken, this test will also be broken
    as.process_announcement(ann1);
    if (as.already_received(ann1) && !as.already_received(ann2)) {
        return true;
    }
    return false;
}

/** Test the constructor for the ROVppAnnouncement struct
 *
 * @ return True for success
 */
bool test_rovpp_announcement(){
    ROVppAnnouncement ann = ROVppAnnouncement(111, 0x01010101, 0xffffff00, 222, 100, 1);
    if (ann.origin != 111 
        || ann.prefix.addr != 0x01010101 
        || ann.prefix.netmask != 0xffffff00 
        || ann.received_from_asn != 222 
        || ann.priority != 0 
        || ann.from_monitor != false 
        || ann.tstamp != 100
        || ann.policy_index != 1) {
        return false;
    }
    
    ann = ROVppAnnouncement(111, 0x01010101, 0xffffff00, 262, 222, 100, 1, true);
    if (ann.origin != 111 
        || ann.prefix.addr != 0x01010101 
        || ann.prefix.netmask != 0xffffff00 
        || ann.received_from_asn != 222 
        || ann.priority != 262 
        || ann.from_monitor != true 
        || ann.tstamp != 100
        || ann.policy_index != 1) {
        return false;
    }

    return true;
}
