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

#ifndef BASE_AS_H
#define BASE_AS_H

#define AS_REL_PROVIDER 0
#define AS_REL_PEER 100
#define AS_REL_CUSTOMER 200

#include <type_traits>
#include <string>
#include <set>
#include <map>
#include <vector>
#include <random>
#include <iostream>

#include "Logger.h"
#include "Prefix.h"

#include "Announcements/Announcement.h"
#include "Announcements/ROVppAnnouncement.h"

template <class AnnouncementType>
class BaseAS {

static_assert(std::is_base_of<Announcement, AnnouncementType>::value, "AnnouncementType must inherit from Announcement");

public:
    uint32_t asn;       // Autonomous System Number
    bool visited;       // Marks something
    int rank;           // Rank in ASGraph heirarchy for propagation 
    // Random Number Generator
    std::minstd_rand ran_bool;
    // Defer processing of incoming announcements for efficiency
    std::vector<AnnouncementType> *incoming_announcements;
    // Maps of all announcements stored
    std::map<Prefix<>, AnnouncementType> *all_anns;
    std::map<Prefix<>, AnnouncementType> *depref_anns;
    // Stores AS Relationships
    std::set<uint32_t> *providers; 
    std::set<uint32_t> *peers; 
    std::set<uint32_t> *customers; 
    // Pointer to inverted results map for efficiency
    std::map<std::pair<Prefix<>, uint32_t>,std::set<uint32_t>*> *inverse_results; 
    // If this AS represents multiple ASes, it's "members" are listed here (Supernodes)
    std::vector<uint32_t> *member_ases;
    // Assigned and used in Tarjan's algorithm
    int index;
    int lowlink;
    bool onStack;
    
    // Constructor
    BaseAS(uint32_t myasn=0, 
        std::map<std::pair<Prefix<>, uint32_t>,std::set<uint32_t>*> *inv=NULL,
        std::set<uint32_t> *prov=NULL, 
        std::set<uint32_t> *peer=NULL,
        std::set<uint32_t> *cust=NULL) : ran_bool(myasn) {

        // Set ASN
        asn = myasn;
        // Initialize AS to invalid rank
        rank = -1;     
        
        // Create AS relationship sets
        if (prov == NULL) {
            providers = new std::set<uint32_t>;
        } else {
            providers = prov;
        }

        if (peer == NULL) {
            peers = new std::set<uint32_t>;
        } else {
            peers = peer;
        }

        if (cust == NULL) {
            customers = new std::set<uint32_t>;
        } else {
            customers = cust;
        }

        inverse_results = inv;                      // Inverted results map
        member_ases = new std::vector<uint32_t>;    // Supernode members
        incoming_announcements = new std::vector<AnnouncementType>;
        all_anns = new std::map<Prefix<>, AnnouncementType>;
        depref_anns = new std::map<Prefix<>, AnnouncementType>;
        // Tarjan variables
        index = -1;
        onStack = false;
    }
    ~BaseAS();
    
    bool get_random(); 
    void add_neighbor(uint32_t asn, int relationship);
    void remove_neighbor(uint32_t asn, int relationship);
    void receive_announcements(std::vector<AnnouncementType> &announcements);
    void process_announcement(AnnouncementType &ann, bool ran=true);
    void process_announcements(bool ran=true);
    void clear_announcements();
    bool already_received(AnnouncementType &ann);
    void delete_ann(AnnouncementType &ann);
    void printDebug();
    void swap_inverse_result(std::pair<Prefix<>,uint32_t> old, 
                             std::pair<Prefix<>,uint32_t> current);

    template <class U>
    friend std::ostream& operator<<(std::ostream &os, const BaseAS<U>& as);
    std::ostream& stream_announcements(std:: ostream &os);
    std::ostream& stream_depref(std:: ostream &os);
};
#endif