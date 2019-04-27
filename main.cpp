#ifndef RUN_TESTS
#include <iostream>
#include <boost/program_options.hpp>

#include "AS.h"
#include "ASGraph.h"
#include "Announcement.h"
#include "Extrapolator.h"
#include "Tests.h"


int main(int argc, char *argv[]) {
    using namespace std;   
    namespace po = boost::program_options;

    #ifdef RUN_TESTS
    // Graph Tests
    //as_relationship_test();
    //as_receive_test();
    //tarjan_accuracy_test();
    //tarjan_size_test();
    //tarjan_on_real_data();
    //combine_components_test();
    //fully_create_graph_test();

    // Propagation Tests
    //as_process_test();
    //as_process_test_2();
    //send_all_test();
    //give_ann_to_as_path_test();
    //propagate_up_test();
    //propagate_down_test();
    //full_propagation_test_a();
    //full_propagation_test_b();
    //
    // SQL Tests
    //test_db_connection();
    //announcement_comparison_test();
    //SQL_insertion_test();
    //
    // Results Tests
    //
    //decide_ranks_test(); 
    //select_all_test();
    //stub_test();
    //distinct_prefixes_test();
    //find_as_path();
    cout << "All tests run successfully." << endl;
    #endif
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("invert-results,i", po::value<bool>()->default_value(true), 
          "record ASNs which do *not* have a route to a prefix-origin (smaller results size)")
        ("ram", po::value<bool>()->default_value(false), 
          "use the RAM tablespace (it must exist in the db)")
        ("results-table,r", po::value<string>()->default_value(RESULTS_TABLE),
          "name of the results table")
        ("inverse-results-table,o",
          po::value<string>()->default_value(INVERSE_RESULTS_TABLE),
          "name of the inverse results table")
        ("announcements-table,a",
          po::value<string>()->default_value(ANNOUNCEMENTS_TABLE),
          "name of announcements table");
        //("batch-size", po::value<int>(&batch_size)->default_value(100),
        // "number of prefixes to be used in one propagation cycle")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc,argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")){
        cout << desc << endl;
        exit(0);
    }

    Extrapolator *extrap = new Extrapolator(vm["invert-results"].as<bool>(),
        (vm.count("announcements-table") ? vm["announcements-table"].as<string>() : ANNOUNCEMENTS_TABLE),
        (vm.count("results-table") ? vm["results-table"].as<string>() : RESULTS_TABLE),
        (vm.count("inverse-results-table") ? vm["inverse-results-table"].as<string>() : INVERSE_RESULTS_TABLE),
        (vm.count("ram") ? vm["ram"].as<bool>() : false));
    // TODO make 100 an option, make 800k something more reasonable
    extrap->perform_propagation(true, 100, 10000000);
    delete extrap;

    return 0;
}
#endif // RUN_TESTS
