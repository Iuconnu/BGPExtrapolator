#ifndef TESTS_H
#define TESTS_H

#include <iostream>
#include <fstream>
#include <string>
#include <string.h>
#include <algorithm>
#include <assert.h>
#include <random>
#include <map>
#include <typeinfo>
#include <cmath>
#include <chrono>

#include "SQLQuerier.h"
#include "AS.h"
#include "ASGraph.h"
#include "Announcement.h"
#include "Extrapolator.h"
#include "Prefix.h"

void as_relationship_test();
void tarjan_accuracy_test();
void tarjan_size_test();
void as_receive_test();
void as_process_test();
void set_comparison_test();
void test_db_connection();
void send_all_test();
void tarjan_on_real_data(bool save_large_component = false);
void combine_components_test();
void decide_ranks_test();
ASGraph* create_graph_from_files();
void fully_create_graph_test();
void give_ann_to_as_path_test();
void propagate_up_test();
void propagate_down_test();
void select_all_test();
void full_extrapolation_test();
void query_array_test();
#endif
