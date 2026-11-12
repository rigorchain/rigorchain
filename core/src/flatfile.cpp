// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cerrno>
#include <cstdint>
#include <limits>
#include <stdexcept>

#include <flatfile.h>
#include <logging.h>
#include <tinyformat.h>
#include <util/fs_helpers.h>

FlatFileSeq::FlatFileSeq(fs::path dir, const char* prefix, size_t chunk_size) :
    m_dir(std::move(dir)),
    m_prefix(prefix),
    m_chunk_size(chunk_size)
{
    if (chunk_size == 0) {
        throw std::invalid_argument("chunk_size must be positive");
    }
}

std::string FlatFilePos::ToString() const
{
    return strprintf("FlatFilePos(nFile=%i, nPos=%llu)", nFile, static_cast<unsigned long long>(nPos));
}

fs::path FlatFileSeq::FileName(const FlatFilePos& pos) const
{
    return m_dir / fs::u8path(strprintf("%s%05u.dat", m_prefix, pos.nFile));
}

FILE* FlatFileSeq::Open(const FlatFilePos& pos, bool read_only)
{
    if (pos.IsNull()) {
        return nullptr;
    }
    fs::path path = FileName(pos);
    fs::create_directories(path.parent_path());
    FILE* file = fsbridge::fopen(path, read_only ? "rb": "rb+");
    if (!file && !read_only)
        file = fsbridge::fopen(path, "wb+");
    if (!file) {
        LogPrintf("Unable to open file %s\n", fs::PathToString(path));
        return nullptr;
    }
    if (pos.nPos) {
        if (pos.nPos > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) ||
            FileSeek64(file, static_cast<int64_t>(pos.nPos), SEEK_SET) != 0) {
            LogPrintf("Unable to seek to position %llu of %s\n", static_cast<unsigned long long>(pos.nPos), fs::PathToString(path));
            fclose(file);
            return nullptr;
        }
    }
    return file;
}

size_t FlatFileSeq::Allocate(const FlatFilePos& pos, size_t add_size, bool& out_of_space)
{
    out_of_space = false;

    if (add_size > std::numeric_limits<uint64_t>::max() - pos.nPos) {
        LogPrintf("FlatFileSeq::Allocate position overflow file=%i pos=%llu add=%llu\n",
                  pos.nFile, static_cast<unsigned long long>(pos.nPos), static_cast<unsigned long long>(add_size));
        return 0;
    }
    const uint64_t chunk = m_chunk_size;
    auto chunks_covering = [chunk](uint64_t value) -> uint64_t {
        if (value == 0) return 0;
        if (value > std::numeric_limits<uint64_t>::max() - (chunk - 1)) {
            return std::numeric_limits<uint64_t>::max();
        }
        return (value + chunk - 1) / chunk;
    };
    const uint64_t end = pos.nPos + static_cast<uint64_t>(add_size);
    const uint64_t n_old_chunks = chunks_covering(pos.nPos);
    const uint64_t n_new_chunks = chunks_covering(end);
    if (n_new_chunks == std::numeric_limits<uint64_t>::max() ||
        n_new_chunks > std::numeric_limits<uint64_t>::max() / chunk) {
        LogPrintf("FlatFileSeq::Allocate chunk overflow file=%i pos=%llu\n",
                  pos.nFile, static_cast<unsigned long long>(pos.nPos));
        return 0;
    }
    if (n_new_chunks > n_old_chunks) {
        const uint64_t old_size = pos.nPos;
        const uint64_t new_size = n_new_chunks * chunk;
        if (new_size < old_size || new_size - old_size > std::numeric_limits<size_t>::max()) {
            LogPrintf("FlatFileSeq::Allocate size overflow file=%i pos=%llu\n",
                      pos.nFile, static_cast<unsigned long long>(pos.nPos));
            return 0;
        }
        const size_t inc_size = static_cast<size_t>(new_size - old_size);

        if (CheckDiskSpace(m_dir, inc_size)) {
            FILE *file = Open(pos);
            if (file) {
                LogPrint(BCLog::VALIDATION, "Pre-allocating up to position 0x%llx in %s%05u.dat\n", static_cast<unsigned long long>(new_size), m_prefix, pos.nFile);
                AllocateFileRange(file, pos.nPos, inc_size);
                fclose(file);
                return inc_size;
            }
        } else {
            out_of_space = true;
        }
    }
    return 0;
}

bool FlatFileSeq::Flush(const FlatFilePos& pos, bool finalize)
{
    FILE* file = Open(FlatFilePos(pos.nFile, 0)); // Avoid fseek to nPos
    if (!file) {
        return error("%s: failed to open file %d", __func__, pos.nFile);
    }
    if (finalize && !TruncateFile(file, pos.nPos)) {
        fclose(file);
        return error("%s: failed to truncate file %d", __func__, pos.nFile);
    }
    if (!FileCommit(file)) {
        fclose(file);
        return error("%s: failed to commit file %d", __func__, pos.nFile);
    }
    DirectoryCommit(m_dir);

    fclose(file);
    return true;
}
