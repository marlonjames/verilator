// -*- mode: C++; c-file-style: "cc-mode" -*-
//*************************************************************************
//
// Copyright 2020 by Wilson Snyder and Marlon James. This program is free software; you can
// redistribute it and/or modify it under the terms of either the GNU
// Lesser General Public License Version 3 or the Perl Artistic License
// Version 2.0.
// SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0
//
//*************************************************************************

#ifdef IS_VPI

#include "vpi_user.h"
#include <cstdlib>

#else

#include "Vt_vpi_cb_valuechange.h"
#include "verilated.h"
#include "verilated_vpi.h"

#endif

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "TestSimulator.h"
#include "TestVpi.h"

const std::vector<int> cbs_to_test{cbValueChange};

enum CallbackState { PRE_REGISTER, ACTIVE, ACTIVE_AGAIN, REM_REREG_ACTIVE, POST_REMOVE };
const std::vector<CallbackState> cb_states{PRE_REGISTER, ACTIVE, ACTIVE_AGAIN, REM_REREG_ACTIVE,
                                           POST_REMOVE};

#define CB_COUNT cbAtEndOfSimTime + 1
TestVpiHandle vh_cb;

unsigned int callback_count = 0;
unsigned int callback_expected_count = 0;

bool callbacks_called = false;
bool callbacks_expected_called = false;

std::vector<CallbackState>::const_iterator state_iter;

unsigned int main_time = 0;
bool got_error = false;

#ifdef IS_VPI
vpiHandle vh_clk;
#endif

#ifdef TEST_VERBOSE
bool verbose = true;
#else
bool verbose = false;
#endif

#define CHECK_RESULT_NZ(got) \
    if (!(got)) { \
        printf("%%Error: %s:%d: GOT = NULL  EXP = !NULL\n", __FILE__, __LINE__); \
        got_error = true; \
        return __LINE__; \
    }

// Use cout to avoid issues with %d/%lx etc
#define CHECK_RESULT(got, exp) \
    if ((got) != (exp)) { \
        std::cout << std::dec << "%Error: " << __FILE__ << ":" << __LINE__ << ": GOT = " << (got) \
                  << "   EXP = " << (exp) << std::endl; \
        got_error = true; \
        return __LINE__; \
    }

static int the_callback(p_cb_data cb_data) {
    callback_count = callback_count + 1;
    return 0;
}

static int register_cb(const int next_state) {
    t_cb_data cb_data_testcase;
    s_vpi_value v;  // Needed in this scope as is in cb_data
    bzero(&cb_data_testcase, sizeof(cb_data_testcase));
    cb_data_testcase.cb_rtn = the_callback;
    cb_data_testcase.reason = cbValueChange;

#ifdef IS_VPI
    vpiHandle count_h = vpi_handle_by_name((PLI_BYTE8*)"t.count", 0);
#else
    TestVpiHandle count_h = VPI_HANDLE("count");  // Needed in this scope as is in cb_data
#endif
    CHECK_RESULT_NZ(count_h);
    v.format = vpiSuppressVal;

    cb_data_testcase.obj = count_h;
    cb_data_testcase.value = &v;

    // State of callback next time through loop
    if (verbose) vpi_printf(const_cast<char*>("     Updating callback for next loop:\n"));
    switch (next_state) {
    case ACTIVE: {
        if (verbose) {
            vpi_printf(const_cast<char*>("     - Registering callback cbValueChange\n"));
        }
        vh_cb.release();
        vh_cb = vpi_register_cb(&cb_data_testcase);
        break;
    }
    case REM_REREG_ACTIVE: {
        if (verbose) {
            vpi_printf(
                const_cast<char*>("     - Removing callback cbValueChange and re-registering\n"));
        }
        int ret = vpi_remove_cb(vh_cb);
        vh_cb.freed();
        CHECK_RESULT(ret, 1);
        vh_cb = vpi_register_cb(&cb_data_testcase);
        break;
    }
    case POST_REMOVE: {
        if (verbose) { vpi_printf(const_cast<char*>("     - Removing callback cbValueChange\n")); }
        int ret = vpi_remove_cb(vh_cb);
        vh_cb.freed();
        CHECK_RESULT(ret, 1);
        break;
    }
    default:
        if (verbose) vpi_printf(const_cast<char*>("     - No change\n"));
        break;
    }

    return 0;
}

static int test_callbacks(p_cb_data cb_data) {
    t_cb_data cb_data_testcase;
    bzero(&cb_data_testcase, sizeof(cb_data_testcase));

    if (verbose) vpi_printf(const_cast<char*>("     Checking callback results\n"));

    // Check results from previous loop
    auto count = callback_count;
    auto exp_count = callback_expected_count;
    CHECK_RESULT(count, exp_count);

#if !defined(IS_VPI)
    bool called = callbacks_called;
    bool exp_called = callbacks_expected_called;
    CHECK_RESULT(called, exp_called);
#endif

    // Update expected values based on state of callback in next time through main loop
    callbacks_expected_called = false;

    const int current_state = *state_iter;
    const int next_state = (current_state + 1) % cb_states.size();

    switch (next_state) {
    case PRE_REGISTER:
    case ACTIVE:
    case ACTIVE_AGAIN:
    case REM_REREG_ACTIVE: {
        callback_expected_count = callback_expected_count + 1;
        callbacks_expected_called = true;
        break;
    }
    default: break;
    }

    int ret = register_cb(next_state);
    if (ret) return ret;

    // Update iterators for next loop
    ++state_iter;

    // Re-register this cb for next time step
    if (state_iter != cb_states.cend()) {
        if (verbose) {
            vpi_printf(const_cast<char*>("     Re-registering test_callbacks for next loop\n"));
        }
        t_cb_data cb_data_n;
        bzero(&cb_data_n, sizeof(cb_data_n));
        s_vpi_time t1;

        cb_data_n.reason = cbAfterDelay;
        t1.type = vpiSimTime;
        t1.high = 0;
        t1.low = 10;
        cb_data_n.time = &t1;
        cb_data_n.cb_rtn = test_callbacks;
        TestVpiHandle vh_test_cb = vpi_register_cb(&cb_data_n);
        CHECK_RESULT_NZ(vh_test_cb);
    }

    return ret;
}

#ifdef IS_VPI
// Toggle the clock in other simulators using VPI
static int toggle_clock(p_cb_data data) {
    s_vpi_value val;
    s_vpi_time time = {vpiSimTime, 0, 0, 0};

    val.format = vpiIntVal;
    vpi_get_value(vh_clk, &val);
    val.value.integer = !val.value.integer;
    vpi_put_value(vh_clk, &val, &time, vpiInertialDelay);

    s_vpi_time cur_time = {vpiSimTime, 0, 0, 0};
    vpi_get_time(0, &cur_time);

    if (cur_time.low < 100 && !got_error) {
        t_cb_data cb_data;
        bzero(&cb_data, sizeof(cb_data));
        time.low = 5;
        cb_data.reason = cbAfterDelay;
        cb_data.time = &time;
        cb_data.cb_rtn = toggle_clock;
        vpi_register_cb(&cb_data);
    }

    return 0;
}
#endif

static int register_test_callback(p_cb_data data) {
    t_cb_data cb_data;
    bzero(&cb_data, sizeof(cb_data));
    s_vpi_time t1;

    if (verbose) vpi_printf(const_cast<char*>("     Registering test_cbs Timed callback\n"));

    cb_data.reason = cbAfterDelay;
    t1.type = vpiSimTime;
    t1.high = 0;
    t1.low = 10;
    cb_data.time = &t1;
    cb_data.cb_rtn = test_callbacks;
    TestVpiHandle vh_test_cb = vpi_register_cb(&cb_data);
    CHECK_RESULT_NZ(vh_test_cb);

    state_iter = cb_states.cbegin();

#ifdef IS_VPI
    t1.low = 1;
    cb_data.cb_rtn = toggle_clock;
    TestVpiHandle vh_toggle_cb = vpi_register_cb(&cb_data);
    CHECK_RESULT_NZ(vh_toggle_cb);

    vh_clk = vpi_handle_by_name((PLI_BYTE8*)"t.clk", 0);
    CHECK_RESULT_NZ(vh_clk);
#endif

    return 0;
}

#ifdef IS_VPI

static int end_of_sim_cb(p_cb_data cb_data) {
    if (!got_error) { fprintf(stdout, "*-* All Finished *-*\n"); }
    return 0;
}

// cver entry
void vpi_compat_bootstrap(void) {
    t_cb_data cb_data;
    bzero(&cb_data, sizeof(cb_data));
    {
        vpi_printf(const_cast<char*>("register start-of-sim callback\n"));
        cb_data.reason = cbStartOfSimulation;
        cb_data.time = 0;
        cb_data.cb_rtn = register_test_callback;
        vpi_register_cb(&cb_data);
    }
    {
        cb_data.reason = cbEndOfSimulation;
        cb_data.time = 0;
        cb_data.cb_rtn = end_of_sim_cb;
        vpi_register_cb(&cb_data);
    }
}
// icarus entry
void (*vlog_startup_routines[])() = {vpi_compat_bootstrap, 0};

#else

double sc_time_stamp() { return main_time; }

int main(int argc, char** argv, char** env) {
    vluint64_t sim_time = 100;
    bool cbs_called;
    Verilated::commandArgs(argc, argv);

    VM_PREFIX* topp = new VM_PREFIX("");  // Note null name - we're flattening it out

    if (verbose) VL_PRINTF("-- { Sim Time %d } --\n", main_time);

    register_test_callback(nullptr);

    topp->eval();
    topp->clk = 0;
    main_time += 1;

    while (vl_time_stamp64() < sim_time && !Verilated::gotFinish()) {
        if (verbose) {
            VL_PRINTF("-- { Sim Time %d , Callback cbValueChange (1) , Testcase State %d } --\n",
                      main_time, *state_iter);
        }

        topp->eval();

        if (verbose) { VL_PRINTF("     Calling cbValueChange (1) callbacks\t"); }
        cbs_called = VerilatedVpi::callValueCbs();
        if (verbose) VL_PRINTF(" - any callbacks called? %s\n", cbs_called ? "YES" : "NO");
        callbacks_called = cbs_called;

        VerilatedVpi::callTimedCbs();

        main_time = VerilatedVpi::cbNextDeadline();
        if (main_time == -1 && !Verilated::gotFinish()) {
            if (verbose) VL_PRINTF("-- { Sim Time %d , No more testcases } --\n", main_time);
            if (got_error) {
                vl_stop(__FILE__, __LINE__, "TOP-cpp");
            } else {
                VL_PRINTF("*-* All Finished *-*\n");
                Verilated::gotFinish(true);
            }
        }

        // Count updates on rising edge, so cycle through falling edge as well
        topp->clk = !topp->clk;
        topp->eval();
        topp->clk = !topp->clk;
    }

    if (!Verilated::gotFinish()) {
        vl_fatal(__FILE__, __LINE__, "main", "%Error: Timeout; never got a $finish");
    }
    topp->final();

    VL_DO_DANGLING(delete topp, topp);
    return 0;
}

#endif
