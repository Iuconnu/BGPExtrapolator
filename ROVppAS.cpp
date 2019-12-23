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

#include "ROVppAS.h"

ROVppAS::ROVppAS(uint32_t myasn,
                 ROVppASGraph *as_graph,
                 std::map<std::pair<Prefix<>, uint32_t>,std::set<uint32_t>*> *inv,
                 std::set<uint32_t> *prov,
                 std::set<uint32_t> *peer,
                 std::set<uint32_t> *cust): 
                 AS(myasn, inv, prov, peer, cust)  {
                    // Save reference to as_graph
                    // Will be used to check who the attackers are
                    rovpp_as_graph = as_graph;
                 }

ROVppAS::~ROVppAS() { }


/** Adds a policy to the policy_vector
 *
 * This function allows you to specify the policies
 * that this AS implements. The different types of policies are listed in 
 * the header of this class. 
 * 
 * @param p The policy to add. For example, ROVPPAS_TYPE_BGP (defualt), and
 * ROVPPAS_TYPE_ROV. Check header for other policies.
 */
void ROVppAS::add_policy(uint32_t p) {
    policy_vector.push_back(p);
}

/**
 * Checks whether or not an announcement is from an attacker
 * 
 * @param  ann [description]
 * @return bool  true if from attacker, false otherwise
 */
bool ROVppAS::pass_rov(Announcement &ann) {
    auto result = rovpp_as_graph->attackers->find(ann.origin);
    if (result == rovpp_as_graph->attackers->end()) {
        return false;
    } else {
        return true;
    }
}

/** Push the received announcements to the incoming_announcements vector.
 *
 * Note that this differs from the Python version in that it does not store
 * a dict of (prefix -> list of announcements for that prefix).
 *
 * @param announcements to be pushed onto the incoming_announcements vector.
 */
void ROVppAS::receive_announcements(std::vector<Announcement> &announcements) {
    for (Announcement &ann : announcements) {
        // Check if ROV is in policy_vector
        if (policy_vector[0] == ROVPPAS_TYPE_ROV) {
        // Check if the Announcement is from attacker
            if (pass_rov(ann)) {
                // Do not check for duplicates here
                // push_back makes a copy of the announcement
                incoming_announcements->push_back(ann);
            }
        }
    }
}
