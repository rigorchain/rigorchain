// Copyright (c) 2019-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <clientversion.h>
#include <chain.h>
#include <flatfile.h>
#include <streams.h>
#include <test/util/setup_common.h>
#include <util/fs_helpers.h>
#include <util/system.h>

#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <cstring>
#include <limits>

#ifndef WIN32
#include <sys/stat.h>
#endif

BOOST_FIXTURE_TEST_SUITE(flatfile_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(flatfile_filename)
{
    const auto data_dir = m_args.GetDataDirBase();

    FlatFilePos pos(456, 789);

    FlatFileSeq seq1(data_dir, "a", 16 * 1024);
    BOOST_CHECK_EQUAL(seq1.FileName(pos), data_dir / "a00456.dat");

    FlatFileSeq seq2(data_dir / "a", "b", 16 * 1024);
    BOOST_CHECK_EQUAL(seq2.FileName(pos), data_dir / "a" / "b00456.dat");

    // Check default constructor IsNull
    assert(FlatFilePos{}.IsNull());
}

BOOST_AUTO_TEST_CASE(flatfile_open)
{
    const auto data_dir = m_args.GetDataDirBase();
    FlatFileSeq seq(data_dir, "a", 16 * 1024);

    std::string line1("A purely peer-to-peer version of electronic cash would allow online "
                      "payments to be sent directly from one party to another without going "
                      "through a financial institution.");
    std::string line2("Digital signatures provide part of the solution, but the main benefits are "
                      "lost if a trusted third party is still required to prevent double-spending.");

    size_t pos1 = 0;
    size_t pos2 = pos1 + GetSerializeSize(line1, CLIENT_VERSION);

    // Write first line to file.
    {
        AutoFile file{seq.Open(FlatFilePos(0, pos1))};
        file << LIMITED_STRING(line1, 256);
    }

    // Attempt to append to file opened in read-only mode.
    {
        AutoFile file{seq.Open(FlatFilePos(0, pos2), true)};
        BOOST_CHECK_THROW(file << LIMITED_STRING(line2, 256), std::ios_base::failure);
    }

    // Append second line to file.
    {
        AutoFile file{seq.Open(FlatFilePos(0, pos2))};
        file << LIMITED_STRING(line2, 256);
    }

    // Read text from file in read-only mode.
    {
        std::string text;
        AutoFile file{seq.Open(FlatFilePos(0, pos1), true)};

        file >> LIMITED_STRING(text, 256);
        BOOST_CHECK_EQUAL(text, line1);

        file >> LIMITED_STRING(text, 256);
        BOOST_CHECK_EQUAL(text, line2);
    }

    // Read text from file with position offset.
    {
        std::string text;
        AutoFile file{seq.Open(FlatFilePos(0, pos2))};

        file >> LIMITED_STRING(text, 256);
        BOOST_CHECK_EQUAL(text, line2);
    }

    // Ensure another file in the sequence has no data.
    {
        std::string text;
        AutoFile file{seq.Open(FlatFilePos(1, pos2))};
        BOOST_CHECK_THROW(file >> LIMITED_STRING(text, 256), std::ios_base::failure);
    }
}

BOOST_AUTO_TEST_CASE(flatfile_allocate)
{
    const auto data_dir = m_args.GetDataDirBase();
    FlatFileSeq seq(data_dir, "a", 100);

    bool out_of_space;

    BOOST_CHECK_EQUAL(seq.Allocate(FlatFilePos(0, 0), 1, out_of_space), 100U);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 0))), 100U);
    BOOST_CHECK(!out_of_space);

    BOOST_CHECK_EQUAL(seq.Allocate(FlatFilePos(0, 99), 1, out_of_space), 0U);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 99))), 100U);
    BOOST_CHECK(!out_of_space);

    BOOST_CHECK_EQUAL(seq.Allocate(FlatFilePos(0, 99), 2, out_of_space), 101U);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 99))), 200U);
    BOOST_CHECK(!out_of_space);
}

BOOST_AUTO_TEST_CASE(flatfile_flush)
{
    const auto data_dir = m_args.GetDataDirBase();
    FlatFileSeq seq(data_dir, "a", 100);

    bool out_of_space;
    seq.Allocate(FlatFilePos(0, 0), 1, out_of_space);

    // Flush without finalize should not truncate file.
    seq.Flush(FlatFilePos(0, 1));
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 1))), 100U);

    // Flush with finalize should truncate file.
    seq.Flush(FlatFilePos(0, 1), true);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 1))), 1U);
}

BOOST_AUTO_TEST_CASE(flatfile_position_varint_matches_historical_uint32)
{
    const uint32_t legacy_undo = 3000000000u;
    CDataStream legacy(SER_DISK, CLIENT_VERSION);
    uint32_t blocks = 2;
    uint32_t size = 1000;
    uint32_t height_first = 1;
    uint32_t height_last = 2;
    uint64_t time_first = 10;
    uint64_t time_last = 20;
    legacy << VARINT(blocks) << VARINT(size) << VARINT(legacy_undo) << VARINT(height_first) << VARINT(height_last) << VARINT(time_first) << VARINT(time_last);

    CBlockFileInfo info;
    info.SetNull();
    info.nBlocks = blocks;
    info.nSize = size;
    info.nUndoSize = legacy_undo;
    info.nHeightFirst = height_first;
    info.nHeightLast = height_last;
    info.nTimeFirst = time_first;
    info.nTimeLast = time_last;
    CDataStream modern(SER_DISK, CLIENT_VERSION);
    modern << info;
    BOOST_CHECK_EQUAL(legacy.size(), modern.size());
    BOOST_CHECK(legacy.size() == modern.size() && std::memcmp(legacy.data(), modern.data(), legacy.size()) == 0);

    CDataStream reader(Span<const std::byte>(legacy.data(), legacy.size()), SER_DISK, CLIENT_VERSION);
    CBlockFileInfo loaded;
    reader >> loaded;
    BOOST_CHECK_EQUAL(loaded.nUndoSize, static_cast<uint64_t>(legacy_undo));
    BOOST_CHECK_EQUAL(loaded.nSize, static_cast<uint64_t>(size));

    CDataStream wide(SER_DISK, CLIENT_VERSION);
    const FlatFilePos above_4gib(4, (uint64_t{1} << 32) + 32);
    wide << above_4gib;
    FlatFilePos roundtrip;
    wide >> roundtrip;
    BOOST_CHECK(roundtrip == above_4gib);
}

static void WriteAndReadAt(FlatFileSeq& seq, uint64_t offset, char marker)
{
    {
        AutoFile file{seq.Open(FlatFilePos(0, offset))};
        BOOST_REQUIRE(!file.IsNull());
        BOOST_CHECK_EQUAL(FileTell64(file.Get()), static_cast<int64_t>(offset));
        BOOST_REQUIRE_EQUAL(fwrite(&marker, 1, 1, file.Get()), 1u);
        BOOST_CHECK_EQUAL(FileTell64(file.Get()), static_cast<int64_t>(offset + 1));
    }
    {
        char got = 0;
        AutoFile file{seq.Open(FlatFilePos(0, offset), true)};
        BOOST_REQUIRE(!file.IsNull());
        BOOST_REQUIRE_EQUAL(fread(&got, 1, 1, file.Get()), 1u);
        BOOST_CHECK_EQUAL(got, marker);
    }
}

BOOST_AUTO_TEST_CASE(flatfile_sparse_offsets_past_2gib_and_4gib)
{
    const auto data_dir = m_args.GetDataDirBase();
    FlatFileSeq seq(data_dir, "rev", 16 * 1024);
    const uint64_t below_2gib = 1024;
    const uint64_t past_2gib = (uint64_t{1} << 31) + 16;
    const uint64_t past_4gib = (uint64_t{1} << 32) + 32;

    {
        AutoFile created{seq.Open(FlatFilePos(0, 0))};
        BOOST_REQUIRE(!created.IsNull());
        BOOST_REQUIRE(TruncateFile(created.Get(), past_4gib + 1));
    }
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 0))), past_4gib + 1);
#ifndef WIN32
    struct stat st;
    BOOST_REQUIRE_EQUAL(::stat(fs::PathToString(seq.FileName(FlatFilePos(0, 0))).c_str(), &st), 0);
    BOOST_CHECK_LT(static_cast<uint64_t>(st.st_blocks) * 512, uint64_t{1} << 20);
#endif

    WriteAndReadAt(seq, below_2gib, 'L');
    WriteAndReadAt(seq, past_2gib, 'M');
    WriteAndReadAt(seq, past_4gib, 'H');

    BOOST_REQUIRE(seq.Flush(FlatFilePos(0, past_4gib + 1), true));
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 0))), past_4gib + 1);
    {
        char got = 0;
        AutoFile file{seq.Open(FlatFilePos(0, past_4gib), true)};
        BOOST_REQUIRE(!file.IsNull());
        BOOST_REQUIRE_EQUAL(fread(&got, 1, 1, file.Get()), 1u);
        BOOST_CHECK_EQUAL(got, 'H');
    }

    BOOST_REQUIRE(seq.Flush(FlatFilePos(0, past_4gib), true));
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 0))), past_4gib);
    {
        char got = 0;
        AutoFile file{seq.Open(FlatFilePos(0, past_4gib), true)};
        BOOST_REQUIRE(!file.IsNull());
        BOOST_CHECK_EQUAL(fread(&got, 1, 1, file.Get()), 0u);
    }
    {
        char got = 0;
        AutoFile file{seq.Open(FlatFilePos(0, past_2gib), true)};
        BOOST_REQUIRE(!file.IsNull());
        BOOST_REQUIRE_EQUAL(fread(&got, 1, 1, file.Get()), 1u);
        BOOST_CHECK_EQUAL(got, 'M');
    }

    BOOST_CHECK(seq.Open(FlatFilePos(0, std::numeric_limits<uint64_t>::max())) == nullptr);
    BOOST_CHECK_EQUAL(fs::file_size(seq.FileName(FlatFilePos(0, 0))), past_4gib);
    BOOST_CHECK(!TruncateFile(nullptr, 1));
    BOOST_CHECK_EQUAL(FileSeek64(nullptr, 0, SEEK_SET), -1);

    FILE* read_only = fsbridge::fopen(seq.FileName(FlatFilePos(0, 0)), "rb");
    BOOST_REQUIRE(read_only != nullptr);
    BOOST_CHECK(!TruncateFile(read_only, past_2gib));
    fclose(read_only);
}

BOOST_AUTO_TEST_SUITE_END()
