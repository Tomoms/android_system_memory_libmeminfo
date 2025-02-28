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

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include <meminfo/androidprocheaps.h>
#include <meminfo/pageacct.h>
#include <meminfo/procmeminfo.h>
#include <meminfo/sysmeminfo.h>
#include <vintf/VintfObject.h>

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

using namespace std;
using namespace android::meminfo;
using android::vintf::KernelVersion;
using android::vintf::RuntimeInfo;
using android::vintf::VintfObject;

namespace fs = std::filesystem;

pid_t pid = -1;

TEST(ProcMemInfo, TestWorkingTestReset) {
    // Expect reset to succeed
    EXPECT_TRUE(ProcMemInfo::ResetWorkingSet(pid));
}

TEST(ProcMemInfo, UsageEmpty) {
    // If we created the object for getting working set,
    // the usage must be empty
    ProcMemInfo proc_mem(pid, true);
    const MemUsage& usage = proc_mem.Usage();
    EXPECT_EQ(usage.rss, 0);
    EXPECT_EQ(usage.vss, 0);
    EXPECT_EQ(usage.pss, 0);
    EXPECT_EQ(usage.uss, 0);
    EXPECT_EQ(usage.swap, 0);
}

TEST(ProcMemInfo, MapsNotEmpty) {
    // Make sure the process maps are never empty
    ProcMemInfo proc_mem(pid);
    const std::vector<Vma>& maps = proc_mem.Maps();
    EXPECT_FALSE(maps.empty());
}

TEST(ProcMemInfo, MapsUsageNotEmpty) {
    ProcMemInfo proc_mem(pid);
    const std::vector<Vma>& maps = proc_mem.Maps();
    EXPECT_FALSE(maps.empty());
    uint64_t total_pss = 0;
    uint64_t total_rss = 0;
    uint64_t total_uss = 0;
    for (auto& map : maps) {
        ASSERT_NE(0, map.usage.vss);
        total_rss += map.usage.rss;
        total_pss += map.usage.pss;
        total_uss += map.usage.uss;
    }

    // Crude check that stats are actually being read.
    EXPECT_NE(0, total_rss) << "RSS zero for all maps, that is not possible.";
    EXPECT_NE(0, total_pss) << "PSS zero for all maps, that is not possible.";
    EXPECT_NE(0, total_uss) << "USS zero for all maps, that is not possible.";
}

TEST(ProcMemInfo, MapsUsageEmpty) {
    ProcMemInfo proc_mem(pid);
    const std::vector<Vma>& maps = proc_mem.MapsWithoutUsageStats();
    EXPECT_FALSE(maps.empty());
    // Verify that all usage stats are zero in every map.
    for (auto& map : maps) {
        ASSERT_EQ(0, map.usage.vss);
        ASSERT_EQ(0, map.usage.rss);
        ASSERT_EQ(0, map.usage.pss);
        ASSERT_EQ(0, map.usage.uss);
        ASSERT_EQ(0, map.usage.swap);
        ASSERT_EQ(0, map.usage.swap_pss);
        ASSERT_EQ(0, map.usage.private_clean);
        ASSERT_EQ(0, map.usage.private_dirty);
        ASSERT_EQ(0, map.usage.shared_clean);
        ASSERT_EQ(0, map.usage.shared_dirty);
    }
}

TEST(ProcMemInfo, MapsUsageFillInLater) {
    ProcMemInfo proc_mem(pid);
    const std::vector<Vma>& maps = proc_mem.MapsWithoutUsageStats();
    EXPECT_FALSE(maps.empty());
    for (auto& map : maps) {
        Vma update_map(map);
        ASSERT_EQ(map.start, update_map.start);
        ASSERT_EQ(map.end, update_map.end);
        ASSERT_EQ(map.offset, update_map.offset);
        ASSERT_EQ(map.flags, update_map.flags);
        ASSERT_EQ(map.name, update_map.name);
        ASSERT_EQ(0, update_map.usage.vss);
        ASSERT_EQ(0, update_map.usage.rss);
        ASSERT_EQ(0, update_map.usage.pss);
        ASSERT_EQ(0, update_map.usage.uss);
        ASSERT_EQ(0, update_map.usage.swap);
        ASSERT_EQ(0, update_map.usage.swap_pss);
        ASSERT_EQ(0, update_map.usage.private_clean);
        ASSERT_EQ(0, update_map.usage.private_dirty);
        ASSERT_EQ(0, update_map.usage.shared_clean);
        ASSERT_EQ(0, update_map.usage.shared_dirty);
        ASSERT_TRUE(proc_mem.FillInVmaStats(update_map));
        // Check that at least one usage stat was updated.
        ASSERT_NE(0, update_map.usage.vss);
    }
}

TEST(ProcMemInfo, MapsUsageFillInAll) {
    ProcMemInfo proc_mem(pid);
    const std::vector<Vma>& maps = proc_mem.MapsWithoutUsageStats();
    EXPECT_FALSE(maps.empty());
    for (auto& map : maps) {
        ASSERT_EQ(0, map.usage.vss);
        ASSERT_EQ(0, map.usage.rss);
        ASSERT_EQ(0, map.usage.pss);
        ASSERT_EQ(0, map.usage.uss);
        ASSERT_EQ(0, map.usage.swap);
        ASSERT_EQ(0, map.usage.swap_pss);
        ASSERT_EQ(0, map.usage.private_clean);
        ASSERT_EQ(0, map.usage.private_dirty);
        ASSERT_EQ(0, map.usage.shared_clean);
        ASSERT_EQ(0, map.usage.shared_dirty);
    }
    // GetUsageStats' non-default parameter get_wss is false by default in
    // ProcMemInfo's constructor.
    ASSERT_TRUE(proc_mem.GetUsageStats(false));
    for (auto& map : maps) {
        // Check that at least one usage stat was updated.
        ASSERT_NE(0, map.usage.vss);
    }
}

TEST(ProcMemInfo, PageMapPresent) {
    static constexpr size_t kNumPages = 20;
    size_t pagesize = getpagesize();
    void* ptr = mmap(nullptr, pagesize * (kNumPages + 2), PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(MAP_FAILED, ptr);

    // Unmap the first page and the last page so that we guarantee this
    // map is in a map by itself.
    ASSERT_EQ(0, munmap(ptr, pagesize));
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr) + pagesize;
    ASSERT_EQ(0, munmap(reinterpret_cast<void*>(addr + kNumPages * pagesize), pagesize));

    ProcMemInfo proc_mem(getpid());
    const std::vector<Vma>& maps = proc_mem.MapsWithoutUsageStats();
    ASSERT_FALSE(maps.empty());

    // Find the vma associated with our previously created map.
    const Vma* test_vma = nullptr;
    for (const Vma& vma : maps) {
        if (vma.start == addr) {
            test_vma = &vma;
            break;
        }
    }
    ASSERT_TRUE(test_vma != nullptr) << "Cannot find test map.";

    // Verify that none of the pages are listed as present.
    std::vector<uint64_t> pagemap;
    ASSERT_TRUE(proc_mem.PageMap(*test_vma, &pagemap));
    ASSERT_EQ(kNumPages, pagemap.size());
    for (size_t i = 0; i < pagemap.size(); i++) {
        EXPECT_FALSE(android::meminfo::page_present(pagemap[i]))
                << "Page " << i << " is present and it should not be.";
    }

    // Make some of the pages present and verify that we see them
    // as present.
    uint8_t* data = reinterpret_cast<uint8_t*>(addr);
    data[0] = 1;
    data[pagesize * 5] = 1;
    data[pagesize * 11] = 1;

    ASSERT_TRUE(proc_mem.PageMap(*test_vma, &pagemap));
    ASSERT_EQ(kNumPages, pagemap.size());
    for (size_t i = 0; i < pagemap.size(); i++) {
        if (i == 0 || i == 5 || i == 11) {
            EXPECT_TRUE(android::meminfo::page_present(pagemap[i]))
                    << "Page " << i << " is not present and it should be.";
        } else {
            EXPECT_FALSE(android::meminfo::page_present(pagemap[i]))
                    << "Page " << i << " is present and it should not be.";
        }
    }

    ASSERT_EQ(0, munmap(reinterpret_cast<void*>(addr), kNumPages * pagesize));
}

TEST(ProcMemInfo, WssEmpty) {
    // If we created the object for getting usage,
    // the working set must be empty
    ProcMemInfo proc_mem(pid, false);
    const MemUsage& wss = proc_mem.Wss();
    EXPECT_EQ(wss.rss, 0);
    EXPECT_EQ(wss.vss, 0);
    EXPECT_EQ(wss.pss, 0);
    EXPECT_EQ(wss.uss, 0);
    EXPECT_EQ(wss.swap, 0);
}

TEST(ProcMemInfo, SwapOffsetsEmpty) {
    // If we created the object for getting working set,
    // the swap offsets must be empty
    ProcMemInfo proc_mem(pid, true);
    const std::vector<uint64_t>& swap_offsets = proc_mem.SwapOffsets();
    EXPECT_EQ(swap_offsets.size(), 0);
}

TEST(ProcMemInfo, IsSmapsSupportedTest) {
    // Check if /proc/self/smaps_rollup exists using the API.
    bool supported = IsSmapsRollupSupported();
    EXPECT_EQ(!access("/proc/self/smaps_rollup", F_OK | R_OK), supported);
}

TEST(ProcMemInfo, SmapsOrRollupTest) {
    // Make sure we can parse 'smaps_rollup' correctly
    std::string rollup =
            R"rollup(12c00000-7fe859e000 ---p 00000000 00:00 0                                [rollup]
Rss:              331908 kB
Pss:              202052 kB
Shared_Clean:     158492 kB
Shared_Dirty:      18928 kB
Private_Clean:     90472 kB
Private_Dirty:     64016 kB
Referenced:       318700 kB
Anonymous:         81984 kB
AnonHugePages:         0 kB
Shared_Hugetlb:        0 kB
Private_Hugetlb:       0 kB
Swap:               5344 kB
SwapPss:             442 kB
Locked:          1523537 kB)rollup";

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(rollup, tf.fd));

    MemUsage stats;
    ASSERT_EQ(SmapsOrRollupFromFile(tf.path, &stats), true);
    EXPECT_EQ(stats.rss, 331908);
    EXPECT_EQ(stats.pss, 202052);
    EXPECT_EQ(stats.uss, 154488);
    EXPECT_EQ(stats.private_clean, 90472);
    EXPECT_EQ(stats.private_dirty, 64016);
    EXPECT_EQ(stats.swap_pss, 442);
}

TEST(ProcMemInfo, SmapsOrRollupSmapsTest) {
    // Make sure /proc/<pid>/smaps is parsed correctly
    std::string smaps =
            R"smaps(12c00000-13440000 rw-p 00000000 00:00 0                                  [anon:dalvik-main space (region space)]
Name:           [anon:dalvik-main space (region space)]
Size:               8448 kB
KernelPageSize:        4 kB
MMUPageSize:           4 kB
Rss:                2652 kB
Pss:                2652 kB
Shared_Clean:        840 kB
Shared_Dirty:         40 kB
Private_Clean:        84 kB
Private_Dirty:      2652 kB
Referenced:         2652 kB
Anonymous:          2652 kB
AnonHugePages:         0 kB
ShmemPmdMapped:        0 kB
Shared_Hugetlb:        0 kB
Private_Hugetlb:       0 kB
Swap:                102 kB
SwapPss:              70 kB
Locked:             2652 kB
VmFlags: rd wr mr mw me ac 
)smaps";

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(smaps, tf.fd));

    MemUsage stats;
    ASSERT_EQ(SmapsOrRollupFromFile(tf.path, &stats), true);
    EXPECT_EQ(stats.rss, 2652);
    EXPECT_EQ(stats.pss, 2652);
    EXPECT_EQ(stats.uss, 2736);
    EXPECT_EQ(stats.private_clean, 84);
    EXPECT_EQ(stats.private_dirty, 2652);
    EXPECT_EQ(stats.swap_pss, 70);
}

TEST(ProcMemInfo, SmapsOrRollupPssRollupTest) {
    // Make sure /proc/<pid>/smaps is parsed correctly
    // to get the PSS
    std::string smaps =
            R"smaps(12c00000-13440000 rw-p 00000000 00:00 0                                  [anon:dalvik-main space (region space)]
Name:           [anon:dalvik-main space (region space)]
Size:               8448 kB
KernelPageSize:        4 kB
MMUPageSize:           4 kB
Rss:                2652 kB
Pss:                2652 kB
Shared_Clean:        840 kB
Shared_Dirty:         40 kB
Private_Clean:        84 kB
Private_Dirty:      2652 kB
Referenced:         2652 kB
Anonymous:          2652 kB
AnonHugePages:         0 kB
ShmemPmdMapped:        0 kB
Shared_Hugetlb:        0 kB
Private_Hugetlb:       0 kB
Swap:                102 kB
SwapPss:              70 kB
Locked:             2652 kB
VmFlags: rd wr mr mw me ac 
)smaps";

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(smaps, tf.fd));

    uint64_t pss;
    ASSERT_EQ(SmapsOrRollupPssFromFile(tf.path, &pss), true);
    EXPECT_EQ(pss, 2652);
}

TEST(ProcMemInfo, SmapsOrRollupPssSmapsTest) {
    // Correctly parse smaps file to gather pss
    std::string exec_dir = ::android::base::GetExecutableDirectory();
    std::string path = ::android::base::StringPrintf("%s/testdata1/smaps_short", exec_dir.c_str());

    uint64_t pss;
    ASSERT_EQ(SmapsOrRollupPssFromFile(path, &pss), true);
    EXPECT_EQ(pss, 19119);
}

TEST(ProcMemInfo, StatusVmRSSTest) {
    std::string exec_dir = ::android::base::GetExecutableDirectory();
    std::string path = ::android::base::StringPrintf("%s/testdata1/status", exec_dir.c_str());

    uint64_t rss;
    ASSERT_EQ(StatusVmRSSFromFile(path, &rss), true);
    EXPECT_EQ(rss, 730764);
}

TEST(ProcMemInfo, StatusVmRSSBogusFileTest) {
    std::string exec_dir = ::android::base::GetExecutableDirectory();
    std::string path = ::android::base::StringPrintf("%s/testdata1/smaps", exec_dir.c_str());

    uint64_t rss;
    ASSERT_EQ(StatusVmRSSFromFile(path, &rss), false);
}

TEST(ProcMemInfo, ForEachExistingVmaTest) {
    std::string exec_dir = ::android::base::GetExecutableDirectory();
    std::string path = ::android::base::StringPrintf("%s/testdata1/smaps_short", exec_dir.c_str());
    ProcMemInfo proc_mem(pid);
    // Populate maps_.
    proc_mem.Smaps(path);
    std::vector<Vma> vmas;
    auto collect_vmas = [&](const Vma& v) {
        vmas.push_back(v);
        return true;
    };
    EXPECT_TRUE(proc_mem.ForEachExistingVma(collect_vmas));

    // The size of vmas is not checked because Smaps() will return 5 vmas on x86
    // and 6 vmas otherwise, as [vsyscall] is not processed on x86.

    // Expect values to be equal to what we have in testdata1/smaps_short
    // Check for names
    EXPECT_EQ(vmas[0].name, "[anon:dalvik-zygote-jit-code-cache]");
    EXPECT_EQ(vmas[1].name, "/system/framework/x86_64/boot-framework.art");
    EXPECT_TRUE(vmas[2].name == "[anon:libc_malloc]" ||
                android::base::StartsWith(vmas[2].name, "[anon:scudo:"))
            << "Unknown map name " << vmas[2].name;
    EXPECT_EQ(vmas[3].name, "/system/priv-app/SettingsProvider/oat/x86_64/SettingsProvider.odex");
    EXPECT_EQ(vmas[4].name, "/system/lib64/libhwui.so");

    // Check start address
    EXPECT_EQ(vmas[0].start, 0x54c00000);
    EXPECT_EQ(vmas[1].start, 0x701ea000);
    EXPECT_EQ(vmas[2].start, 0x70074dd8d000);
    EXPECT_EQ(vmas[3].start, 0x700755a2d000);
    EXPECT_EQ(vmas[4].start, 0x7007f85b0000);

    // Check end address
    EXPECT_EQ(vmas[0].end, 0x56c00000);
    EXPECT_EQ(vmas[1].end, 0x70cdb000);
    EXPECT_EQ(vmas[2].end, 0x70074ee0d000);
    EXPECT_EQ(vmas[3].end, 0x700755a6e000);
    EXPECT_EQ(vmas[4].end, 0x7007f8b9b000);

    // Check Flags
    EXPECT_EQ(vmas[0].flags, PROT_READ | PROT_EXEC);
    EXPECT_EQ(vmas[1].flags, PROT_READ | PROT_WRITE);
    EXPECT_EQ(vmas[2].flags, PROT_READ | PROT_WRITE);
    EXPECT_EQ(vmas[3].flags, PROT_READ | PROT_EXEC);
    EXPECT_EQ(vmas[4].flags, PROT_READ | PROT_EXEC);

    // Check Shared
    EXPECT_FALSE(vmas[0].is_shared);
    EXPECT_FALSE(vmas[1].is_shared);
    EXPECT_FALSE(vmas[2].is_shared);
    EXPECT_FALSE(vmas[3].is_shared);
    EXPECT_FALSE(vmas[4].is_shared);

    // Check Offset
    EXPECT_EQ(vmas[0].offset, 0x0);
    EXPECT_EQ(vmas[1].offset, 0x0);
    EXPECT_EQ(vmas[2].offset, 0x0);
    EXPECT_EQ(vmas[3].offset, 0x00016000);
    EXPECT_EQ(vmas[4].offset, 0x001ee000);

    // Check Inode
    EXPECT_EQ(vmas[0].inode, 0);
    EXPECT_EQ(vmas[1].inode, 3165);
    EXPECT_EQ(vmas[2].inode, 0);
    EXPECT_EQ(vmas[3].inode, 1947);
    EXPECT_EQ(vmas[4].inode, 1537);

    // Check smaps specific fields
    ASSERT_EQ(vmas[0].usage.vss, 32768);
    EXPECT_EQ(vmas[1].usage.vss, 11204);
    EXPECT_EQ(vmas[2].usage.vss, 16896);
    EXPECT_EQ(vmas[3].usage.vss, 260);
    EXPECT_EQ(vmas[4].usage.vss, 6060);

    EXPECT_EQ(vmas[0].usage.rss, 2048);
    EXPECT_EQ(vmas[1].usage.rss, 11188);
    EXPECT_EQ(vmas[2].usage.rss, 15272);
    EXPECT_EQ(vmas[3].usage.rss, 260);
    EXPECT_EQ(vmas[4].usage.rss, 4132);

    EXPECT_EQ(vmas[0].usage.pss, 113);
    EXPECT_EQ(vmas[1].usage.pss, 2200);
    EXPECT_EQ(vmas[2].usage.pss, 15272);
    EXPECT_EQ(vmas[3].usage.pss, 260);
    EXPECT_EQ(vmas[4].usage.pss, 1274);

    EXPECT_EQ(vmas[0].usage.uss, 0);
    EXPECT_EQ(vmas[1].usage.uss, 1660);
    EXPECT_EQ(vmas[2].usage.uss, 15272);
    EXPECT_EQ(vmas[3].usage.uss, 260);
    EXPECT_EQ(vmas[4].usage.uss, 0);

    EXPECT_EQ(vmas[0].usage.private_clean, 0);
    EXPECT_EQ(vmas[1].usage.private_clean, 0);
    EXPECT_EQ(vmas[2].usage.private_clean, 0);
    EXPECT_EQ(vmas[3].usage.private_clean, 260);
    EXPECT_EQ(vmas[4].usage.private_clean, 0);

    EXPECT_EQ(vmas[0].usage.private_dirty, 0);
    EXPECT_EQ(vmas[1].usage.private_dirty, 1660);
    EXPECT_EQ(vmas[2].usage.private_dirty, 15272);
    EXPECT_EQ(vmas[3].usage.private_dirty, 0);
    EXPECT_EQ(vmas[4].usage.private_dirty, 0);

    EXPECT_EQ(vmas[0].usage.shared_clean, 0);
    EXPECT_EQ(vmas[1].usage.shared_clean, 80);
    EXPECT_EQ(vmas[2].usage.shared_clean, 0);
    EXPECT_EQ(vmas[3].usage.shared_clean, 0);
    EXPECT_EQ(vmas[4].usage.shared_clean, 4132);

    EXPECT_EQ(vmas[0].usage.shared_dirty, 2048);
    EXPECT_EQ(vmas[1].usage.shared_dirty, 9448);
    EXPECT_EQ(vmas[2].usage.shared_dirty, 0);
    EXPECT_EQ(vmas[3].usage.shared_dirty, 0);
    EXPECT_EQ(vmas[4].usage.shared_dirty, 0);

    EXPECT_EQ(vmas[0].usage.swap, 0);
    EXPECT_EQ(vmas[1].usage.swap, 0);
    EXPECT_EQ(vmas[2].usage.swap, 0);
    EXPECT_EQ(vmas[3].usage.swap, 0);
    EXPECT_EQ(vmas[4].usage.swap, 0);

    EXPECT_EQ(vmas[0].usage.swap_pss, 0);
    EXPECT_EQ(vmas[1].usage.swap_pss, 0);
    EXPECT_EQ(vmas[2].usage.swap_pss, 0);
    EXPECT_EQ(vmas[3].usage.swap_pss, 0);
    EXPECT_EQ(vmas[4].usage.swap_pss, 0);

#ifndef __x86_64__
    // vmas[5] will not exist on x86, as [vsyscall] would not be processed.
    EXPECT_EQ(vmas[5].name, "[vsyscall]");
    EXPECT_EQ(vmas[5].start, 0xffffffffff600000);
    EXPECT_EQ(vmas[5].end, 0xffffffffff601000);
    EXPECT_EQ(vmas[5].flags, PROT_READ | PROT_EXEC);
    EXPECT_FALSE(vmas[5].is_shared);
    EXPECT_EQ(vmas[5].offset, 0x0);
    EXPECT_EQ(vmas[5].inode, 0);
    EXPECT_EQ(vmas[5].usage.vss, 4);
    EXPECT_EQ(vmas[5].usage.rss, 0);
    EXPECT_EQ(vmas[5].usage.pss, 0);
    EXPECT_EQ(vmas[5].usage.uss, 0);
    EXPECT_EQ(vmas[5].usage.private_clean, 0);
    EXPECT_EQ(vmas[5].usage.private_dirty, 0);
    EXPECT_EQ(vmas[5].usage.shared_clean, 0);
    EXPECT_EQ(vmas[5].usage.shared_dirty, 0);
    EXPECT_EQ(vmas[5].usage.swap, 0);
    EXPECT_EQ(vmas[5].usage.swap_pss, 0);
#endif
}

TEST(ProcMemInfo, ForEachVmaFromFile_SmapsTest) {
    // Parse smaps file correctly to make callbacks for each virtual memory area (vma)
    std::string exec_dir = ::android::base::GetExecutableDirectory();
    std::string path = ::android::base::StringPrintf("%s/testdata1/smaps_short", exec_dir.c_str());
    ProcMemInfo proc_mem(pid);

    std::vector<Vma> vmas;
    auto collect_vmas = [&](const Vma& v) {
        vmas.push_back(v);
        return true;
    };
    ASSERT_TRUE(ForEachVmaFromFile(path, collect_vmas));

    // We should get a total of 6 vmas
    ASSERT_EQ(vmas.size(), 6);

    // Expect values to be equal to what we have in testdata1/smaps_short
    // Check for names
    EXPECT_EQ(vmas[0].name, "[anon:dalvik-zygote-jit-code-cache]");
    EXPECT_EQ(vmas[1].name, "/system/framework/x86_64/boot-framework.art");
    EXPECT_TRUE(vmas[2].name == "[anon:libc_malloc]" ||
                android::base::StartsWith(vmas[2].name, "[anon:scudo:"))
            << "Unknown map name " << vmas[2].name;
    EXPECT_EQ(vmas[3].name, "/system/priv-app/SettingsProvider/oat/x86_64/SettingsProvider.odex");
    EXPECT_EQ(vmas[4].name, "/system/lib64/libhwui.so");
    EXPECT_EQ(vmas[5].name, "[vsyscall]");

    // Check start address
    EXPECT_EQ(vmas[0].start, 0x54c00000);
    EXPECT_EQ(vmas[1].start, 0x701ea000);
    EXPECT_EQ(vmas[2].start, 0x70074dd8d000);
    EXPECT_EQ(vmas[3].start, 0x700755a2d000);
    EXPECT_EQ(vmas[4].start, 0x7007f85b0000);
    EXPECT_EQ(vmas[5].start, 0xffffffffff600000);

    // Check end address
    EXPECT_EQ(vmas[0].end, 0x56c00000);
    EXPECT_EQ(vmas[1].end, 0x70cdb000);
    EXPECT_EQ(vmas[2].end, 0x70074ee0d000);
    EXPECT_EQ(vmas[3].end, 0x700755a6e000);
    EXPECT_EQ(vmas[4].end, 0x7007f8b9b000);
    EXPECT_EQ(vmas[5].end, 0xffffffffff601000);

    // Check Flags
    EXPECT_EQ(vmas[0].flags, PROT_READ | PROT_EXEC);
    EXPECT_EQ(vmas[1].flags, PROT_READ | PROT_WRITE);
    EXPECT_EQ(vmas[2].flags, PROT_READ | PROT_WRITE);
    EXPECT_EQ(vmas[3].flags, PROT_READ | PROT_EXEC);
    EXPECT_EQ(vmas[4].flags, PROT_READ | PROT_EXEC);
    EXPECT_EQ(vmas[5].flags, PROT_READ | PROT_EXEC);

    // Check Shared
    EXPECT_FALSE(vmas[0].is_shared);
    EXPECT_FALSE(vmas[1].is_shared);
    EXPECT_FALSE(vmas[2].is_shared);
    EXPECT_FALSE(vmas[3].is_shared);
    EXPECT_FALSE(vmas[4].is_shared);
    EXPECT_FALSE(vmas[5].is_shared);

    // Check Offset
    EXPECT_EQ(vmas[0].offset, 0x0);
    EXPECT_EQ(vmas[1].offset, 0x0);
    EXPECT_EQ(vmas[2].offset, 0x0);
    EXPECT_EQ(vmas[3].offset, 0x00016000);
    EXPECT_EQ(vmas[4].offset, 0x001ee000);
    EXPECT_EQ(vmas[5].offset, 0x0);

    // Check Inode
    EXPECT_EQ(vmas[0].inode, 0);
    EXPECT_EQ(vmas[1].inode, 3165);
    EXPECT_EQ(vmas[2].inode, 0);
    EXPECT_EQ(vmas[3].inode, 1947);
    EXPECT_EQ(vmas[4].inode, 1537);
    EXPECT_EQ(vmas[5].inode, 0);

    // Check smaps specific fields
    ASSERT_EQ(vmas[0].usage.vss, 32768);
    EXPECT_EQ(vmas[1].usage.vss, 11204);
    EXPECT_EQ(vmas[2].usage.vss, 16896);
    EXPECT_EQ(vmas[3].usage.vss, 260);
    EXPECT_EQ(vmas[4].usage.vss, 6060);
    EXPECT_EQ(vmas[5].usage.vss, 4);

    EXPECT_EQ(vmas[0].usage.rss, 2048);
    EXPECT_EQ(vmas[1].usage.rss, 11188);
    EXPECT_EQ(vmas[2].usage.rss, 15272);
    EXPECT_EQ(vmas[3].usage.rss, 260);
    EXPECT_EQ(vmas[4].usage.rss, 4132);
    EXPECT_EQ(vmas[5].usage.rss, 0);

    EXPECT_EQ(vmas[0].usage.pss, 113);
    EXPECT_EQ(vmas[1].usage.pss, 2200);
    EXPECT_EQ(vmas[2].usage.pss, 15272);
    EXPECT_EQ(vmas[3].usage.pss, 260);
    EXPECT_EQ(vmas[4].usage.pss, 1274);
    EXPECT_EQ(vmas[5].usage.pss, 0);

    EXPECT_EQ(vmas[0].usage.uss, 0);
    EXPECT_EQ(vmas[1].usage.uss, 1660);
    EXPECT_EQ(vmas[2].usage.uss, 15272);
    EXPECT_EQ(vmas[3].usage.uss, 260);
    EXPECT_EQ(vmas[4].usage.uss, 0);
    EXPECT_EQ(vmas[5].usage.uss, 0);

    EXPECT_EQ(vmas[0].usage.private_clean, 0);
    EXPECT_EQ(vmas[1].usage.private_clean, 0);
    EXPECT_EQ(vmas[2].usage.private_clean, 0);
    EXPECT_EQ(vmas[3].usage.private_clean, 260);
    EXPECT_EQ(vmas[4].usage.private_clean, 0);
    EXPECT_EQ(vmas[5].usage.private_clean, 0);

    EXPECT_EQ(vmas[0].usage.private_dirty, 0);
    EXPECT_EQ(vmas[1].usage.private_dirty, 1660);
    EXPECT_EQ(vmas[2].usage.private_dirty, 15272);
    EXPECT_EQ(vmas[3].usage.private_dirty, 0);
    EXPECT_EQ(vmas[4].usage.private_dirty, 0);
    EXPECT_EQ(vmas[5].usage.private_dirty, 0);

    EXPECT_EQ(vmas[0].usage.shared_clean, 0);
    EXPECT_EQ(vmas[1].usage.shared_clean, 80);
    EXPECT_EQ(vmas[2].usage.shared_clean, 0);
    EXPECT_EQ(vmas[3].usage.shared_clean, 0);
    EXPECT_EQ(vmas[4].usage.shared_clean, 4132);
    EXPECT_EQ(vmas[5].usage.shared_clean, 0);

    EXPECT_EQ(vmas[0].usage.shared_dirty, 2048);
    EXPECT_EQ(vmas[1].usage.shared_dirty, 9448);
    EXPECT_EQ(vmas[2].usage.shared_dirty, 0);
    EXPECT_EQ(vmas[3].usage.shared_dirty, 0);
    EXPECT_EQ(vmas[4].usage.shared_dirty, 0);
    EXPECT_EQ(vmas[5].usage.shared_dirty, 0);

    EXPECT_EQ(vmas[0].usage.swap, 0);
    EXPECT_EQ(vmas[1].usage.swap, 0);
    EXPECT_EQ(vmas[2].usage.swap, 0);
    EXPECT_EQ(vmas[3].usage.swap, 0);
    EXPECT_EQ(vmas[4].usage.swap, 0);
    EXPECT_EQ(vmas[5].usage.swap, 0);

    EXPECT_EQ(vmas[0].usage.swap_pss, 0);
    EXPECT_EQ(vmas[1].usage.swap_pss, 0);
    EXPECT_EQ(vmas[2].usage.swap_pss, 0);
    EXPECT_EQ(vmas[3].usage.swap_pss, 0);
    EXPECT_EQ(vmas[4].usage.swap_pss, 0);
    EXPECT_EQ(vmas[5].usage.swap_pss, 0);
}

TEST(ProcMemInfo, ForEachVmaFromFile_MapsTest) {
    // Parse maps file correctly to make callbacks for each virtual memory area (vma)
    std::string exec_dir = ::android::base::GetExecutableDirectory();
    std::string path = ::android::base::StringPrintf("%s/testdata1/maps_short", exec_dir.c_str());
    ProcMemInfo proc_mem(pid);

    std::vector<Vma> vmas;
    auto collect_vmas = [&](const Vma& v) {
        vmas.push_back(v);
        return true;
    };
    ASSERT_TRUE(ForEachVmaFromFile(path, collect_vmas, false));

    // We should get a total of 6 vmas
    ASSERT_EQ(vmas.size(), 6);

    // Expect values to be equal to what we have in testdata1/maps_short
    // Check for names
    EXPECT_EQ(vmas[0].name, "[anon:dalvik-zygote-jit-code-cache]");
    EXPECT_EQ(vmas[1].name, "/system/framework/x86_64/boot-framework.art");
    EXPECT_TRUE(vmas[2].name == "[anon:libc_malloc]" ||
                android::base::StartsWith(vmas[2].name, "[anon:scudo:"))
            << "Unknown map name " << vmas[2].name;
    EXPECT_EQ(vmas[3].name, "/system/priv-app/SettingsProvider/oat/x86_64/SettingsProvider.odex");
    EXPECT_EQ(vmas[4].name, "/system/lib64/libhwui.so");
    EXPECT_EQ(vmas[5].name, "[vsyscall]");

    // Check start address
    EXPECT_EQ(vmas[0].start, 0x54c00000);
    EXPECT_EQ(vmas[1].start, 0x701ea000);
    EXPECT_EQ(vmas[2].start, 0x70074dd8d000);
    EXPECT_EQ(vmas[3].start, 0x700755a2d000);
    EXPECT_EQ(vmas[4].start, 0x7007f85b0000);
    EXPECT_EQ(vmas[5].start, 0xffffffffff600000);

    // Check end address
    EXPECT_EQ(vmas[0].end, 0x56c00000);
    EXPECT_EQ(vmas[1].end, 0x70cdb000);
    EXPECT_EQ(vmas[2].end, 0x70074ee0d000);
    EXPECT_EQ(vmas[3].end, 0x700755a6e000);
    EXPECT_EQ(vmas[4].end, 0x7007f8b9b000);
    EXPECT_EQ(vmas[5].end, 0xffffffffff601000);

    // Check Flags
    EXPECT_EQ(vmas[0].flags, PROT_READ | PROT_EXEC);
    EXPECT_EQ(vmas[1].flags, PROT_READ | PROT_WRITE);
    EXPECT_EQ(vmas[2].flags, PROT_READ | PROT_WRITE);
    EXPECT_EQ(vmas[3].flags, PROT_READ | PROT_EXEC);
    EXPECT_EQ(vmas[4].flags, PROT_READ | PROT_EXEC);
    EXPECT_EQ(vmas[5].flags, PROT_READ | PROT_EXEC);

    // Check Shared
    EXPECT_FALSE(vmas[0].is_shared);
    EXPECT_FALSE(vmas[1].is_shared);
    EXPECT_FALSE(vmas[2].is_shared);
    EXPECT_FALSE(vmas[3].is_shared);
    EXPECT_FALSE(vmas[4].is_shared);
    EXPECT_FALSE(vmas[5].is_shared);

    // Check Offset
    EXPECT_EQ(vmas[0].offset, 0x0);
    EXPECT_EQ(vmas[1].offset, 0x0);
    EXPECT_EQ(vmas[2].offset, 0x0);
    EXPECT_EQ(vmas[3].offset, 0x00016000);
    EXPECT_EQ(vmas[4].offset, 0x001ee000);
    EXPECT_EQ(vmas[5].offset, 0x0);

    // Check Inode
    EXPECT_EQ(vmas[0].inode, 0);
    EXPECT_EQ(vmas[1].inode, 3165);
    EXPECT_EQ(vmas[2].inode, 0);
    EXPECT_EQ(vmas[3].inode, 1947);
    EXPECT_EQ(vmas[4].inode, 1537);
    EXPECT_EQ(vmas[5].inode, 0);
}

TEST(ProcMemInfo, SmapsReturnTest) {
    // Make sure Smaps() is never empty for any process
    ProcMemInfo proc_mem(pid);
    auto vmas = proc_mem.Smaps();
    EXPECT_FALSE(vmas.empty());
}

TEST(ProcMemInfo, SmapsTest) {
    std::string exec_dir = ::android::base::GetExecutableDirectory();
    std::string path = ::android::base::StringPrintf("%s/testdata1/smaps_short", exec_dir.c_str());
    ProcMemInfo proc_mem(pid);
    auto vmas = proc_mem.Smaps(path);

    ASSERT_FALSE(vmas.empty());
#ifndef __x86_64__
    // We should get a total of 6 vmas
    ASSERT_EQ(vmas.size(), 6);
#else
    // We should get a total of 5 vmas ([vsyscall] is excluded)
    ASSERT_EQ(vmas.size(), 5);
#endif

    // Expect values to be equal to what we have in testdata1/smaps_short
    // Check for sizes first
    ASSERT_EQ(vmas[0].usage.vss, 32768);
    EXPECT_EQ(vmas[1].usage.vss, 11204);
    EXPECT_EQ(vmas[2].usage.vss, 16896);
    EXPECT_EQ(vmas[3].usage.vss, 260);
    EXPECT_EQ(vmas[4].usage.vss, 6060);
#ifndef __x86_64__
    EXPECT_EQ(vmas[5].usage.vss, 4);
#endif

    // Check for names
    EXPECT_EQ(vmas[0].name, "[anon:dalvik-zygote-jit-code-cache]");
    EXPECT_EQ(vmas[1].name, "/system/framework/x86_64/boot-framework.art");
    EXPECT_TRUE(vmas[2].name == "[anon:libc_malloc]" ||
                android::base::StartsWith(vmas[2].name, "[anon:scudo:"))
            << "Unknown map name " << vmas[2].name;
    EXPECT_EQ(vmas[3].name, "/system/priv-app/SettingsProvider/oat/x86_64/SettingsProvider.odex");
    EXPECT_EQ(vmas[4].name, "/system/lib64/libhwui.so");
#ifndef __x86_64__
    EXPECT_EQ(vmas[5].name, "[vsyscall]");
#endif

    EXPECT_EQ(vmas[0].usage.rss, 2048);
    EXPECT_EQ(vmas[1].usage.rss, 11188);
    EXPECT_EQ(vmas[2].usage.rss, 15272);
    EXPECT_EQ(vmas[3].usage.rss, 260);
    EXPECT_EQ(vmas[4].usage.rss, 4132);
#ifndef __x86_64__
    EXPECT_EQ(vmas[5].usage.rss, 0);
#endif

    EXPECT_EQ(vmas[0].usage.pss, 113);
    EXPECT_EQ(vmas[1].usage.pss, 2200);
    EXPECT_EQ(vmas[2].usage.pss, 15272);
    EXPECT_EQ(vmas[3].usage.pss, 260);
    EXPECT_EQ(vmas[4].usage.pss, 1274);
#ifndef __x86_64__
    EXPECT_EQ(vmas[5].usage.pss, 0);
#endif

    EXPECT_EQ(vmas[0].usage.uss, 0);
    EXPECT_EQ(vmas[1].usage.uss, 1660);
    EXPECT_EQ(vmas[2].usage.uss, 15272);
    EXPECT_EQ(vmas[3].usage.uss, 260);
    EXPECT_EQ(vmas[4].usage.uss, 0);
#ifndef __x86_64__
    EXPECT_EQ(vmas[5].usage.uss, 0);
#endif

    EXPECT_EQ(vmas[0].usage.private_clean, 0);
    EXPECT_EQ(vmas[1].usage.private_clean, 0);
    EXPECT_EQ(vmas[2].usage.private_clean, 0);
    EXPECT_EQ(vmas[3].usage.private_clean, 260);
    EXPECT_EQ(vmas[4].usage.private_clean, 0);
#ifndef __x86_64__
    EXPECT_EQ(vmas[5].usage.private_clean, 0);
#endif

    EXPECT_EQ(vmas[0].usage.private_dirty, 0);
    EXPECT_EQ(vmas[1].usage.private_dirty, 1660);
    EXPECT_EQ(vmas[2].usage.private_dirty, 15272);
    EXPECT_EQ(vmas[3].usage.private_dirty, 0);
    EXPECT_EQ(vmas[4].usage.private_dirty, 0);
#ifndef __x86_64__
    EXPECT_EQ(vmas[5].usage.private_dirty, 0);
#endif

    EXPECT_EQ(vmas[0].usage.shared_clean, 0);
    EXPECT_EQ(vmas[1].usage.shared_clean, 80);
    EXPECT_EQ(vmas[2].usage.shared_clean, 0);
    EXPECT_EQ(vmas[3].usage.shared_clean, 0);
    EXPECT_EQ(vmas[4].usage.shared_clean, 4132);
#ifndef __x86_64__
    EXPECT_EQ(vmas[5].usage.shared_clean, 0);
#endif

    EXPECT_EQ(vmas[0].usage.shared_dirty, 2048);
    EXPECT_EQ(vmas[1].usage.shared_dirty, 9448);
    EXPECT_EQ(vmas[2].usage.shared_dirty, 0);
    EXPECT_EQ(vmas[3].usage.shared_dirty, 0);
    EXPECT_EQ(vmas[4].usage.shared_dirty, 0);
#ifndef __x86_64__
    EXPECT_EQ(vmas[5].usage.shared_dirty, 0);
#endif

    EXPECT_EQ(vmas[0].usage.swap, 0);
    EXPECT_EQ(vmas[1].usage.swap, 0);
    EXPECT_EQ(vmas[2].usage.swap, 0);
    EXPECT_EQ(vmas[3].usage.swap, 0);
    EXPECT_EQ(vmas[4].usage.swap, 0);
#ifndef __x86_64__
    EXPECT_EQ(vmas[5].usage.swap, 0);
#endif

    EXPECT_EQ(vmas[0].usage.swap_pss, 0);
    EXPECT_EQ(vmas[1].usage.swap_pss, 0);
    EXPECT_EQ(vmas[2].usage.swap_pss, 0);
    EXPECT_EQ(vmas[3].usage.swap_pss, 0);
    EXPECT_EQ(vmas[4].usage.swap_pss, 0);
#ifndef __x86_64__
    EXPECT_EQ(vmas[5].usage.swap_pss, 0);
#endif
}

TEST(ProcMemInfo, SmapsPopulatesUsageTest) {
    std::string exec_dir = ::android::base::GetExecutableDirectory();
    std::string path = ::android::base::StringPrintf("%s/testdata1/smaps_short", exec_dir.c_str());
    ProcMemInfo proc_mem(pid);
    auto vmas = proc_mem.Smaps(path, true);

    // Expect values to be equal to sums of usage in testdata1/smaps_short. For
    // this data, only vss differs on x86.
#ifndef __x86_64__
    EXPECT_EQ(proc_mem.Usage().vss, 67192);
#else
    EXPECT_EQ(proc_mem.Usage().vss, 67188);
#endif
    EXPECT_EQ(proc_mem.Usage().rss, 32900);
    EXPECT_EQ(proc_mem.Usage().pss, 19119);
    EXPECT_EQ(proc_mem.Usage().uss, 17192);
    EXPECT_EQ(proc_mem.Usage().private_clean, 260);
    EXPECT_EQ(proc_mem.Usage().private_dirty, 16932);
    EXPECT_EQ(proc_mem.Usage().shared_clean, 4212);
    EXPECT_EQ(proc_mem.Usage().shared_dirty, 11496);
    EXPECT_EQ(proc_mem.Usage().swap, 0);
    EXPECT_EQ(proc_mem.Usage().swap_pss, 0);
}

TEST(SysMemInfo, TestSysMemInfoFile) {
    std::string meminfo = R"meminfo(MemTotal:        3019740 kB
MemFree:         1809728 kB
MemAvailable:    2546560 kB
Buffers:           54736 kB
Cached:           776052 kB
SwapCached:            0 kB
Active:           445856 kB
Inactive:         459092 kB
Active(anon):      78492 kB
Inactive(anon):     2240 kB
Active(file):     367364 kB
Inactive(file):   456852 kB
Unevictable:        3096 kB
Mlocked:            3096 kB
SwapTotal:         32768 kB
SwapFree:           4096 kB
Dirty:                32 kB
Writeback:             0 kB
AnonPages:         74988 kB
Mapped:            62624 kB
Shmem:              4020 kB
KReclaimable:      87324 kB
Slab:              86464 kB
SReclaimable:      44432 kB
SUnreclaim:        42032 kB
KernelStack:        4880 kB
PageTables:         2900 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:     1509868 kB
Committed_AS:      80296 kB
VmallocTotal:   263061440 kB
VmallocUsed:       65536 kB
VmallocChunk:          0 kB
AnonHugePages:      6144 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
CmaTotal:         131072 kB
CmaFree:          130380 kB
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB)meminfo";

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(meminfo, tf.fd));

    SysMemInfo mi;
    ASSERT_TRUE(mi.ReadMemInfo(tf.path));
    EXPECT_EQ(mi.mem_total_kb(), 3019740);
    EXPECT_EQ(mi.mem_free_kb(), 1809728);
    EXPECT_EQ(mi.mem_buffers_kb(), 54736);
    EXPECT_EQ(mi.mem_cached_kb(), 776052);
    EXPECT_EQ(mi.mem_shmem_kb(), 4020);
    EXPECT_EQ(mi.mem_slab_kb(), 86464);
    EXPECT_EQ(mi.mem_slab_reclaimable_kb(), 44432);
    EXPECT_EQ(mi.mem_slab_unreclaimable_kb(), 42032);
    EXPECT_EQ(mi.mem_swap_kb(), 32768);
    EXPECT_EQ(mi.mem_swap_free_kb(), 4096);
    EXPECT_EQ(mi.mem_mapped_kb(), 62624);
    EXPECT_EQ(mi.mem_vmalloc_used_kb(), 65536);
    EXPECT_EQ(mi.mem_page_tables_kb(), 2900);
    EXPECT_EQ(mi.mem_kernel_stack_kb(), 4880);
    EXPECT_EQ(mi.mem_kreclaimable_kb(), 87324);
    EXPECT_EQ(mi.mem_active_kb(), 445856);
    EXPECT_EQ(mi.mem_inactive_kb(), 459092);
    EXPECT_EQ(mi.mem_unevictable_kb(), 3096);
    EXPECT_EQ(mi.mem_available_kb(), 2546560);
    EXPECT_EQ(mi.mem_active_anon_kb(), 78492);
    EXPECT_EQ(mi.mem_inactive_anon_kb(), 2240);
    EXPECT_EQ(mi.mem_active_file_kb(), 367364);
    EXPECT_EQ(mi.mem_inactive_file_kb(), 456852);
    EXPECT_EQ(mi.mem_cma_total_kb(), 131072);
    EXPECT_EQ(mi.mem_cma_free_kb(), 130380);
}

TEST(SysMemInfo, TestEmptyFile) {
    TemporaryFile tf;
    std::string empty_string = "";
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(empty_string, tf.fd));

    SysMemInfo mi;
    EXPECT_TRUE(mi.ReadMemInfo(tf.path));
    EXPECT_EQ(mi.mem_total_kb(), 0);
}

TEST(SysMemInfo, TestZramTotal) {
    std::string exec_dir = ::android::base::GetExecutableDirectory();

    SysMemInfo mi;
    std::string zram_mmstat_dir = exec_dir + "/testdata1/";
    EXPECT_EQ(mi.mem_zram_kb(zram_mmstat_dir.c_str()), 30504);

    std::string zram_memused_dir = exec_dir + "/testdata2/";
    EXPECT_EQ(mi.mem_zram_kb(zram_memused_dir.c_str()), 30504);
}

enum {
    MEMINFO_TOTAL,
    MEMINFO_FREE,
    MEMINFO_BUFFERS,
    MEMINFO_CACHED,
    MEMINFO_SHMEM,
    MEMINFO_SLAB,
    MEMINFO_SLAB_RECLAIMABLE,
    MEMINFO_SLAB_UNRECLAIMABLE,
    MEMINFO_SWAP_TOTAL,
    MEMINFO_SWAP_FREE,
    MEMINFO_ZRAM_TOTAL,
    MEMINFO_MAPPED,
    MEMINFO_VMALLOC_USED,
    MEMINFO_PAGE_TABLES,
    MEMINFO_KERNEL_STACK,
    MEMINFO_KRECLAIMABLE,
    MEMINFO_ACTIVE,
    MEMINFO_INACTIVE,
    MEMINFO_UNEVICTABLE,
    MEMINFO_AVAILABLE,
    MEMINFO_ACTIVE_ANON,
    MEMINFO_INACTIVE_ANON,
    MEMINFO_ACTIVE_FILE,
    MEMINFO_INACTIVE_FILE,
    MEMINFO_CMA_TOTAL,
    MEMINFO_CMA_FREE,
    MEMINFO_COUNT
};

TEST(SysMemInfo, TestZramWithTags) {
    std::string meminfo = R"meminfo(MemTotal:        3019740 kB
MemFree:         1809728 kB
MemAvailable:    2546560 kB
Buffers:           54736 kB
Cached:           776052 kB
SwapCached:            0 kB
Active:           445856 kB
Inactive:         459092 kB
Active(anon):      78492 kB
Inactive(anon):     2240 kB
Active(file):     367364 kB
Inactive(file):   456852 kB
Unevictable:        3096 kB
Mlocked:            3096 kB
SwapTotal:         32768 kB
SwapFree:           4096 kB
Dirty:                32 kB
Writeback:             0 kB
AnonPages:         74988 kB
Mapped:            62624 kB
Shmem:              4020 kB
KReclaimable:      87324 kB
Slab:              86464 kB
SReclaimable:      44432 kB
SUnreclaim:        42032 kB
KernelStack:        4880 kB
PageTables:         2900 kB
NFS_Unstable:          0 kB
Bounce:                0 kB
WritebackTmp:          0 kB
CommitLimit:     1509868 kB
Committed_AS:      80296 kB
VmallocTotal:   263061440 kB
VmallocUsed:       65536 kB
VmallocChunk:          0 kB
AnonHugePages:      6144 kB
ShmemHugePages:        0 kB
ShmemPmdMapped:        0 kB
CmaTotal:         131072 kB
CmaFree:          130380 kB
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB)meminfo";

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(meminfo, tf.fd));
    std::string file = std::string(tf.path);
    std::vector<uint64_t> mem;
    std::vector<std::string_view> tags(SysMemInfo::kDefaultSysMemInfoTags.begin(),
                                       SysMemInfo::kDefaultSysMemInfoTags.end());
    auto it = tags.begin();
    tags.insert(it + MEMINFO_ZRAM_TOTAL, "Zram:");
    SysMemInfo mi;

    // Read system memory info
    mem.resize(tags.size());
    EXPECT_TRUE(mi.ReadMemInfo(tags.size(), tags.data(), mem.data(), file.c_str()));
    EXPECT_EQ(mem[MEMINFO_TOTAL], 3019740);
    EXPECT_EQ(mem[MEMINFO_FREE], 1809728);
    EXPECT_EQ(mem[MEMINFO_BUFFERS], 54736);
    EXPECT_EQ(mem[MEMINFO_CACHED], 776052);
    EXPECT_EQ(mem[MEMINFO_SHMEM], 4020);
    EXPECT_EQ(mem[MEMINFO_SLAB], 86464);
    EXPECT_EQ(mem[MEMINFO_SLAB_RECLAIMABLE], 44432);
    EXPECT_EQ(mem[MEMINFO_SLAB_UNRECLAIMABLE], 42032);
    EXPECT_EQ(mem[MEMINFO_SWAP_TOTAL], 32768);
    EXPECT_EQ(mem[MEMINFO_SWAP_FREE], 4096);
    EXPECT_EQ(mem[MEMINFO_MAPPED], 62624);
    EXPECT_EQ(mem[MEMINFO_VMALLOC_USED], 65536);
    EXPECT_EQ(mem[MEMINFO_PAGE_TABLES], 2900);
    EXPECT_EQ(mem[MEMINFO_KERNEL_STACK], 4880);
    EXPECT_EQ(mem[MEMINFO_KRECLAIMABLE], 87324);
    EXPECT_EQ(mem[MEMINFO_ACTIVE], 445856);
    EXPECT_EQ(mem[MEMINFO_INACTIVE], 459092);
    EXPECT_EQ(mem[MEMINFO_UNEVICTABLE], 3096);
    EXPECT_EQ(mem[MEMINFO_AVAILABLE], 2546560);
    EXPECT_EQ(mem[MEMINFO_ACTIVE_ANON], 78492);
    EXPECT_EQ(mem[MEMINFO_INACTIVE_ANON], 2240);
    EXPECT_EQ(mem[MEMINFO_ACTIVE_FILE], 367364);
    EXPECT_EQ(mem[MEMINFO_INACTIVE_FILE], 456852);
    EXPECT_EQ(mem[MEMINFO_CMA_TOTAL], 131072);
    EXPECT_EQ(mem[MEMINFO_CMA_FREE], 130380);
}

TEST(SysMemInfo, TestVmallocInfoNoMemory) {
    std::string vmallocinfo =
            R"vmallocinfo(0x0000000000000000-0x0000000000000000   69632 of_iomap+0x78/0xb0 phys=17a00000 ioremap
0x0000000000000000-0x0000000000000000    8192 of_iomap+0x78/0xb0 phys=b220000 ioremap
0x0000000000000000-0x0000000000000000    8192 of_iomap+0x78/0xb0 phys=17c90000 ioremap
0x0000000000000000-0x0000000000000000    8192 of_iomap+0x78/0xb0 phys=17ca0000 ioremap)vmallocinfo";

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(vmallocinfo, tf.fd));
    std::string file = std::string(tf.path);

    EXPECT_EQ(ReadVmallocInfo(file.c_str()), 0);
}

TEST(SysMemInfo, TestVmallocInfoKernel) {
    std::string vmallocinfo =
            R"vmallocinfo(0x0000000000000000-0x0000000000000000    8192 drm_property_create_blob+0x44/0xec pages=1 vmalloc)vmallocinfo";

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(vmallocinfo, tf.fd));
    std::string file = std::string(tf.path);

    EXPECT_EQ(ReadVmallocInfo(file.c_str()), getpagesize());
}

TEST(SysMemInfo, TestVmallocInfoModule) {
    std::string vmallocinfo =
            R"vmallocinfo(0x0000000000000000-0x0000000000000000   28672 pktlog_alloc_buf+0xc4/0x15c [wlan] pages=6 vmalloc)vmallocinfo";

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(vmallocinfo, tf.fd));
    std::string file = std::string(tf.path);

    EXPECT_EQ(ReadVmallocInfo(file.c_str()), 6 * getpagesize());
}

TEST(SysMemInfo, TestVmallocInfoAll) {
    std::string vmallocinfo =
            R"vmallocinfo(0x0000000000000000-0x0000000000000000   69632 of_iomap+0x78/0xb0 phys=17a00000 ioremap
0x0000000000000000-0x0000000000000000    8192 of_iomap+0x78/0xb0 phys=b220000 ioremap
0x0000000000000000-0x0000000000000000    8192 of_iomap+0x78/0xb0 phys=17c90000 ioremap
0x0000000000000000-0x0000000000000000    8192 of_iomap+0x78/0xb0 phys=17ca0000 ioremap
0x0000000000000000-0x0000000000000000    8192 drm_property_create_blob+0x44/0xec pages=1 vmalloc
0x0000000000000000-0x0000000000000000   28672 pktlog_alloc_buf+0xc4/0x15c [wlan] pages=6 vmalloc)vmallocinfo";

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(vmallocinfo, tf.fd));
    std::string file = std::string(tf.path);

    EXPECT_EQ(ReadVmallocInfo(file.c_str()), 7 * getpagesize());
}

TEST(SysMemInfo, TestReadIonHeapsSizeKb) {
    std::string total_heaps_kb = R"total_heaps_kb(98480)total_heaps_kb";
    uint64_t size;

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(total_heaps_kb, tf.fd));
    std::string file = std::string(tf.path);

    ASSERT_TRUE(ReadIonHeapsSizeKb(&size, file));
    EXPECT_EQ(size, 98480);
}

TEST(SysMemInfo, TestReadIonPoolsSizeKb) {
    std::string total_pools_kb = R"total_pools_kb(416)total_pools_kb";
    uint64_t size;

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(total_pools_kb, tf.fd));
    std::string file = std::string(tf.path);

    ASSERT_TRUE(ReadIonPoolsSizeKb(&size, file));
    EXPECT_EQ(size, 416);
}

TEST(SysMemInfo, TestReadGpuTotalUsageKb) {
    uint64_t size;

    if (android::base::GetIntProperty("ro.vendor.api_level", 0) < __ANDROID_API_S__) {
        GTEST_SKIP();
    }

    KernelVersion min_kernel_version = KernelVersion(5, 4, 0);
    KernelVersion kernel_version = VintfObject::GetInstance()
                                           ->getRuntimeInfo(RuntimeInfo::FetchFlag::CPU_VERSION)
                                           ->kernelVersion();
    if (kernel_version < min_kernel_version) {
        GTEST_SKIP();
    }

    ASSERT_TRUE(ReadGpuTotalUsageKb(&size));
    EXPECT_TRUE(size >= 0);
}

TEST(AndroidProcHeaps, ExtractAndroidHeapStatsFromFileTest) {
    std::string smaps =
            R"smaps(12c00000-13440000 rw-p 00000000 00:00 0  [anon:dalvik-main space (region space)]
Name:           [anon:dalvik-main space (region space)]
Size:               8448 kB
KernelPageSize:        4 kB
MMUPageSize:           4 kB
Rss:                2652 kB
Pss:                2652 kB
Shared_Clean:        840 kB
Shared_Dirty:         40 kB
Private_Clean:        84 kB
Private_Dirty:      2652 kB
Referenced:         2652 kB
Anonymous:          2652 kB
AnonHugePages:         0 kB
ShmemPmdMapped:        0 kB
Shared_Hugetlb:        0 kB
Private_Hugetlb:       0 kB
Swap:                102 kB
SwapPss:              70 kB
Locked:             2652 kB
VmFlags: rd wr mr mw me ac
)smaps";

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(smaps, tf.fd));

    bool foundSwapPss;
    AndroidHeapStats stats[_NUM_HEAP];
    ASSERT_TRUE(ExtractAndroidHeapStatsFromFile(tf.path, stats, &foundSwapPss));

    AndroidHeapStats actualStats;
    for (int i = 0; i < _NUM_CORE_HEAP; i++) {
        actualStats.pss += stats[i].pss;
        actualStats.swappablePss += stats[i].swappablePss;
        actualStats.rss += stats[i].rss;
        actualStats.privateDirty += stats[i].privateDirty;
        actualStats.sharedDirty += stats[i].sharedDirty;
        actualStats.privateClean += stats[i].privateClean;
        actualStats.sharedClean += stats[i].sharedClean;
        actualStats.swappedOut += stats[i].swappedOut;
        actualStats.swappedOutPss += stats[i].swappedOutPss;
    }
    EXPECT_EQ(actualStats.pss, 2652);
    EXPECT_EQ(actualStats.swappablePss, 0);
    EXPECT_EQ(actualStats.rss, 2652);
    EXPECT_EQ(actualStats.privateDirty, 2652);
    EXPECT_EQ(actualStats.sharedDirty, 40);
    EXPECT_EQ(actualStats.privateClean, 84);
    EXPECT_EQ(actualStats.sharedClean, 840);
    EXPECT_EQ(actualStats.swappedOut, 102);
    EXPECT_EQ(actualStats.swappedOutPss, 70);
}

class DmabufHeapStats : public ::testing::Test {
  public:
    virtual void SetUp() {
        fs::current_path(fs::temp_directory_path());
        buffer_stats_path = fs::current_path() / "buffers";
        ASSERT_TRUE(fs::create_directory(buffer_stats_path));
        heap_root_path = fs::current_path() / "dma_heap";
        ASSERT_TRUE(fs::create_directory(heap_root_path));
    }
    virtual void TearDown() {
        fs::remove_all(buffer_stats_path);
        fs::remove_all(heap_root_path);
    }

    fs::path buffer_stats_path;
    fs::path heap_root_path;
};

TEST_F(DmabufHeapStats, TestDmabufHeapTotalExportedKb) {
    using android::base::StringPrintf;
    uint64_t size;

    auto system_heap_path = heap_root_path / "system";
    ASSERT_TRUE(android::base::WriteStringToFile("test", system_heap_path));

    for (unsigned int inode_number = 74831; inode_number < 74841; inode_number++) {
        auto buffer_path = buffer_stats_path / StringPrintf("%u", inode_number);
        ASSERT_TRUE(fs::create_directories(buffer_path));

        auto buffer_size_path = buffer_path / "size";
        const std::string buffer_size = "4096";
        ASSERT_TRUE(android::base::WriteStringToFile(buffer_size, buffer_size_path));

        auto exp_name_path = buffer_path / "exporter_name";
        const std::string exp_name = inode_number % 2 ? "system" : "other";
        ASSERT_TRUE(android::base::WriteStringToFile(exp_name, exp_name_path));
    }

    ASSERT_TRUE(ReadDmabufHeapTotalExportedKb(&size, heap_root_path, buffer_stats_path));
    ASSERT_EQ(size, 20);
}

TEST(SysMemInfo, TestReadDmaBufHeapPoolsSizeKb) {
    std::string total_pools_kb = R"total_pools_kb(416)total_pools_kb";
    uint64_t size;

    TemporaryFile tf;
    ASSERT_TRUE(tf.fd != -1);
    ASSERT_TRUE(::android::base::WriteStringToFd(total_pools_kb, tf.fd));
    std::string file = std::string(tf.path);

    ASSERT_TRUE(ReadDmabufHeapPoolsSizeKb(&size, file));
    EXPECT_EQ(size, 416);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::android::base::InitLogging(argv, android::base::StderrLogger);
    pid = getpid();
    return RUN_ALL_TESTS();
}
