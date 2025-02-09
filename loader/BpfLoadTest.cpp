/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android-base/file.h>
#include <android-base/macros.h>
#include <gtest/gtest.h>
#include <libbpf.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include "bpf/BpfMap.h"
#include "bpf/BpfUtils.h"
#include "include/libbpf_android.h"

namespace android {
namespace bpf {

class BpfLoadTest : public ::testing::Test {
  protected:
    BpfLoadTest() {}
    int mProgFd;
    std::string mTpProgPath;
    std::string mTpNeverLoadProgPath;
    std::string mTpMapPath;

    void SetUp() {
        /*
         * b/326156952
         *
         * Kernels prior to 5.11 used rlimit memlock accounting for bpf memory
         * allocations, and therefore require increasing the rlimit of this
         * process for the maps to be created successfully.
         *
         * 5.11 introduces cgroup-based accounting as discussed here:
         * https://lore.kernel.org/bpf/20201201215900.3569844-1-guro@fb.com/
         */
        if (!isAtLeastKernelVersion(5, 11, 0)) EXPECT_EQ(setrlimitForTest(), 0);

        mTpProgPath = "/sys/fs/bpf/prog_bpfLoadTpProg_tracepoint_sched_sched_switch";
        unlink(mTpProgPath.c_str());

        mTpNeverLoadProgPath = "/sys/fs/bpf/prog_bpfLoadTpProg_tracepoint_sched_sched_wakeup";
        unlink(mTpNeverLoadProgPath.c_str());

        mTpMapPath = "/sys/fs/bpf/map_bpfLoadTpProg_cpu_pid_map";
        unlink(mTpMapPath.c_str());

        auto progPath = android::base::GetExecutableDirectory() + "/bpfLoadTpProg.o";
        bool critical = true;

        bpf_prog_type kAllowed[] = {
                BPF_PROG_TYPE_UNSPEC,
        };

        Location loc = {
            .dir = "",
            .prefix = "",
            .allowedProgTypes = kAllowed,
            .allowedProgTypesLength = arraysize(kAllowed),
        };
        EXPECT_EQ(android::bpf::loadProg(progPath.c_str(), &critical, loc), -1);

        ASSERT_EQ(android::bpf::loadProg(progPath.c_str(), &critical), 0);
        EXPECT_EQ(false, critical);

        mProgFd = retrieveProgram(mTpProgPath.c_str());
        ASSERT_GT(mProgFd, 0);

        int ret = bpf_attach_tracepoint(mProgFd, "sched", "sched_switch");
        EXPECT_NE(ret, 0);
    }

    void TearDown() {
        close(mProgFd);
        unlink(mTpProgPath.c_str());
        unlink(mTpMapPath.c_str());
    }

    void checkMapNonZero() {
        // The test program installs a tracepoint on sched:sched_switch
        // and expects the kernel to populate a PID corresponding to CPU
        android::bpf::BpfMap<uint32_t, uint32_t> m(mTpMapPath.c_str());

        // Wait for program to run a little
        sleep(1);

        int non_zero = 0;
        const auto iterFunc = [&non_zero](const uint32_t& key, const uint32_t& val,
                                          BpfMap<uint32_t, uint32_t>& map) {
            if (val && !non_zero) {
                non_zero = 1;
            }

            UNUSED(key);
            UNUSED(map);
            return base::Result<void>();
        };

        EXPECT_RESULT_OK(m.iterateWithValue(iterFunc));
        EXPECT_EQ(non_zero, 1);
    }

    void checkKernelVersionEnforced() {
        ASSERT_EQ(retrieveProgram(mTpNeverLoadProgPath.c_str()), -1);
        ASSERT_EQ(errno, ENOENT);
    }
};

TEST_F(BpfLoadTest, bpfCheckMap) {
    checkMapNonZero();
}

TEST_F(BpfLoadTest, bpfCheckMinKernelVersionEnforced) {
    checkKernelVersionEnforced();
}

}  // namespace bpf
}  // namespace android
