#ifndef RUN_TESTS
#include <iostream>
#include <boost/program_options.hpp>

#include "AS.h"
#include "ASGraph.h"
#include "Announcement.h"
#include "Extrapolator.h"
#include "Tests.h"

void intro() {
    // This needs to be finished
    std::cout << "***** Routing Extrapolator v0.1 *****" << std::endl;
    std::cout << "Copyright (C) someone, somewhere, probably." << std::endl;
    std::cout << "License... is probably important." << std::endl;
    std::cout << "This is free software: you are free to change and redistribute it." << std::endl;
    std::cout << "There is NO WARRANTY, to the extent permitted by law." << std::endl;
}

int main(int argc, char *argv[]) {
    using namespace std;   
    namespace po = boost::program_options;
    // Handle parameters
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("invert-results,i", 
         po::value<bool>()->default_value(true), 
         "record ASNs which do *not* have a route to a prefix-origin (smaller results size)")
        ("store-depref,d", 
         po::value<bool>()->default_value(false), 
         "store depref results")
        ("iteration-size,s", 
         po::value<uint32_t>()->default_value(50000), 
         "number of prefixes to be used in one iteration cycle")
        ("results-table,r",
         po::value<string>()->default_value(RESULTS_TABLE),
         "name of the results table")
        ("depref-table,p", 
         po::value<string>()->default_value(DEPREF_RESULTS_TABLE),
         "name of the depref table")
        ("inverse-results-table,o",
         po::value<string>()->default_value(INVERSE_RESULTS_TABLE),
         "name of the inverse results table")
        ("announcements-table,a",
         po::value<string>()->default_value(ANNOUNCEMENTS_TABLE),
         "name of announcements table")
        ("verification-table,f",
         po::value<string>()->default_value(VERIFICATION_TABLE),
         "name of the verification control table")
        ("verification-as,v",
         po::value<uint32_t>()->default_value(false),
         "a verification monitor AS that will excluded from extrapolation");
    ;
    po::variables_map vm;
    po::store(po::parse_command_line(argc,argv, desc), vm);
    po::notify(vm);
    if (vm.count("help")){
        cout << desc << endl;
        exit(0);
    }
    
    // Handle intro information
    intro();
    
    // Instantiate Extrapolator
    Extrapolator *extrap = new Extrapolator(vm["invert-results"].as<bool>(),
        vm["store-depref"].as<bool>(),
        (vm.count("announcements-table") ? vm["announcements-table"].as<string>() : ANNOUNCEMENTS_TABLE),
        (vm.count("results-table") ? vm["results-table"].as<string>() : RESULTS_TABLE),
        (vm.count("inverse-results-table") ? vm["inverse-results-table"].as<string>() : INVERSE_RESULTS_TABLE),
        (vm.count("depref-table") ? vm["depref-table"].as<string>() : DEPREF_RESULTS_TABLE),
        (vm.count("verification-table") ? vm["verification-table"].as<string>() : VERIFICATION_TABLE),
        (vm["iteration-size"].as<uint32_t>()),
        (vm["verification-as"].as<uint32_t>()));
    
    // Run propagation
    extrap->perform_propagation(true, 100000000000);
    delete extrap;
    return 0;
}
#endif // RUN_TESTS
