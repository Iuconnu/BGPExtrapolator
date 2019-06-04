#include <cstdint>
#include "ROVppAS.h"
#include "NegativeAnnouncement.h"


ROVppAS::ROVppAS(uint32_t myasn,
    std::uint32_t attacker_asn,
    std::uint32_t victim_asn,
    std::string victim_prefix,
    std::map<std::pair<Prefix<>, uint32_t>,std::set<uint32_t>*> *inv,
    ASGraph *graph,
    std::set<uint32_t> *prov,
    std::set<uint32_t> *peer,
    std::set<uint32_t> *cust) {

  // Simulation variables
  this->attacker_asn = attacker_asn;
  this->victim_asn = victim_asn;
  this->victim_prefix = victim_prefix;

  // Save graph reference
  as_graph = graph;

  asn = myasn;
  // Initialize AS to invalid rank
  rank = -1;

  // Generate AS relationship sets
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

  inverse_results = inv;
  member_ases = new std::vector<uint32_t>;
  anns_sent_to_peers_providers = new std::vector<Announcement>;
  incoming_announcements = new std::vector<Announcement>;
  all_anns = new std::map<Prefix<>, Announcement>;
  depref_anns = new std::map<Prefix<>, Announcement>;
  index = -1;
  onStack = false;
}

ROVppAS::~ROVppAS() {
    delete incoming_announcements;
    delete anns_sent_to_peers_providers;
    delete all_anns;
    delete peers;
    delete providers;
    delete customers;
    delete member_ases;
    delete as_graph;
}


bool ROVppAS::pass_rov(Announcement &ann) {
  if (ann.origin == attacker_asn) {
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
        // Check if the Announcement is valid
        if (pass_rov(ann)) {
          // Do not check for duplicates here
          // push_back makes a copy of the announcement
          incoming_announcements->push_back(ann);
        } else {
          // Create a NegativeAnnouncement
          // Extract prefix to Blackhole
          Prefix<> s("137.99.0.0", "255.255.255.0");
          Prefix<> p("137.99.0.0", "255.255.0.0");
          NegativeAnnouncement neg_ann = NegativeAnnouncement(13796, p.addr, p.netmask, 22742, std::set<Prefix<>>());
          neg_ann.null_route_subprefix(s);
          incoming_announcements->push_back(neg_ann);
        }
    }
}


bool ROVppAS::should_make_neg_announcement(Announcement &announcement) {
  // TODO: Implement
}

bool ROVppAS::does_path_intersect(std::vector<uint32_t> p1, std::vector<uint32_t> p2) {

  for (std::size_t i=1; i<p1.size()-1; ++i) {
    for (std::size_t j=1; j<p1.size()-1; ++j) {
      if (p1[i] == p2[j]) {
        return true;
      }
    }
  }
  return false;
}

std::vector<uint32_t> ROVppAS::get_as_path(Announcement &announcement) {
  // will hold the entire path from origin to this node,
  // including itself
  std::vector<uint32_t> as_path;
  uint32_t current_asn = asn;  // it's own ASN
  AS * curr_as = this;
  uint32_t origin_asn = announcement.origin;  // the origin AS of the announcement
  Announcement * curr_announcement = &announcement;
  // Add to the path vector until we reach the origin
  while (current_asn != origin_asn) {
    // append to end of ASN path
    as_path.push_back(current_asn);
    // Get the previous AS in path
    current_asn = curr_announcement->received_from_asn;
    curr_as = as_graph->get_as_with_asn(current_asn);
    // Get same announcement from previous AS
    curr_announcement = curr_as->get_ann_for_prefix(curr_announcement->prefix);
  }
  return as_path;
}