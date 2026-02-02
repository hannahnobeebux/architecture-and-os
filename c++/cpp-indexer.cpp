#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <cstring>

namespace fs = std::filesystem;

// SHA-256 implementation for hashing (public-domain: can be used freely)
#include <array>
#include <vector>
#include <sstream>
#include <iomanip>

class SHA256 {
public:
    static std::string hashFile(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return "";

        SHA256 ctx;
        char buf[8192];
        while (file.read(buf, sizeof(buf)) || file.gcount()) {
            ctx.update(reinterpret_cast<uint8_t*>(buf), file.gcount());
        }
        return ctx.final();
    }

private:
    using uint32 = uint32_t;
    using uint64 = uint64_t;

    std::array<uint32, 8> h = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    std::vector<uint8_t> buffer;
    uint64 bitlen = 0;

    static uint32 rotr(uint32 x, uint32 n) {
        return (x >> n) | (x << (32 - n));
    }

    static uint32 choose(uint32 e, uint32 f, uint32 g) {
        return (e & f) ^ (~e & g);
    }

    static uint32 majority(uint32 a, uint32 b, uint32 c) {
        return (a & b) ^ (a & c) ^ (b & c);
    }

    static uint32 sig0(uint32 x) {
        return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
    }

    static uint32 sig1(uint32 x) {
        return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
    }

    static uint32 ep0(uint32 x) {
        return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
    }

    static uint32 ep1(uint32 x) {
        return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
    }

    void transform(const uint8_t* chunk) {
        static const uint32 k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
            0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
            0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
            0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
            0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
            0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
            0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
            0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
            0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
        };

        uint32 w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (chunk[i * 4] << 24) |
                   (chunk[i * 4 + 1] << 16) |
                   (chunk[i * 4 + 2] << 8) |
                   (chunk[i * 4 + 3]);
        }
        for (int i = 16; i < 64; i++) {
            w[i] = sig1(w[i - 2]) + w[i - 7] + sig0(w[i - 15]) + w[i - 16];
        }

        uint32 a = h[0], b = h[1], c = h[2], d = h[3];
        uint32 e = h[4], f = h[5], g = h[6], hh = h[7];

        for (int i = 0; i < 64; i++) {
            uint32 t1 = hh + ep1(e) + choose(e, f, g) + k[i] + w[i];
            uint32 t2 = ep0(a) + majority(a, b, c);
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void update(const uint8_t* data, size_t len) {
        buffer.insert(buffer.end(), data, data + len);
        bitlen += len * 8;

        while (buffer.size() >= 64) {
            transform(buffer.data());
            buffer.erase(buffer.begin(), buffer.begin() + 64);
        }
    }

    std::string final() {
        buffer.push_back(0x80);
        while (buffer.size() % 64 != 56) buffer.push_back(0);
        for (int i = 7; i >= 0; i--) buffer.push_back((bitlen >> (i * 8)) & 0xff);
        transform(buffer.data());

        std::ostringstream out;
        for (auto x : h) out << std::hex << std::setw(8) << std::setfill('0') << x;
        return out.str();
    }
};

// "Record" is the in-memory data model for one indexed file
// represents one complete index entry, equivalent to one line of the JSONL file for the Python indexer
// stores all metadata collected for a single file, used in the index
struct Record {
    std::string filename;
    std::string path;
    uint64_t size;
    uint64_t mtime;
    std::string hash;
};

//a thread‑safe queue that distributes file indexing tasks among worker threads
//enables parallel execution within a single process.
class JobQueue {
public:
    void push(const fs::path& p) {
        std::lock_guard<std::mutex> lock(m_);
        q_.push(p);
        cv_.notify_one();
    }

    bool pop(fs::path& p) {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&]{ return done_ || !q_.empty(); });

        if (q_.empty()) return false;
        p = q_.front();
        q_.pop();
        return true;
    }

    void done() {
        std::lock_guard<std::mutex> lock(m_);
        done_ = true;
        cv_.notify_all();
    }

private:
    std::queue<fs::path> q_;
    std::mutex m_;
    std::condition_variable cv_;
    bool done_ = false;
};

//this method is executed by each thread
//it repeatedly takes jobs from the shared queue and processes them until no work remains
//it defines how each file is indexed and how the results are stored safely for variant A
static void worker(JobQueue& jobs,
                   std::vector<Record>& records,
                   std::mutex& recMutex)
{
    fs::path p;
    //processes jobs until the queue is empty and marked done
    while (jobs.pop(p)) {
        try {
            //performs indexing for one file (CPU-bound work) eg: reading metadata and computing SHA-256 hash
            Record r;
            r.filename = p.filename().string();
            r.path = p.string();
            r.size = fs::file_size(p);
            r.mtime = fs::last_write_time(p).time_since_epoch().count();
            r.hash = SHA256::hashFile(p);

            //stores the result in the shared records vector (protected by mutex)
            std::lock_guard<std::mutex> lock(recMutex);
            records.push_back(std::move(r));
        }
        catch (...) {
            // ignore unreadable files
        }
    }
}

//this method coordinates the overall indexing process for variant A
//it sets up the job queue, spawns worker threads, and collects the final results
static std::vector<Record> indexDirectory(const fs::path& root, int workers)
{
    JobQueue jobs;
    std::vector<Record> records;
    std::mutex recMutex; //protects records from concurrent writes

    //spawns worker threads
    std::vector<std::thread> threads;
    for (int i = 0; i < workers; ++i) {
        threads.emplace_back(worker,
                             std::ref(jobs),
                             std::ref(records),
                             std::ref(recMutex));
    }

    //recursively scans the directory and enqueues each file (Job instance) for processing
    for (auto& entry : fs::recursive_directory_iterator(root)) {
        if (entry.is_regular_file()) {
            jobs.push(entry.path());
        }
    }

    jobs.done();
    for (auto& t : threads) t.join(); //waits for workers to finish

    //returns all collected records
    //this enables CLI queries such as `find` and `checksum`
    return records;
}

//CLI QUERY: find all files larger than a specified size in MB
static void queryFind(const std::vector<Record>& records, uint64_t minMB)
{
    uint64_t threshold = minMB * 1024ULL * 1024ULL;
    for (const auto& r : records) {
        if (r.size > threshold) {
            std::cout << r.path << " " << r.size << "\n";
        }
    }
}

//CLI QUERY: get the SHA-256 checksum of a specified filename
static void queryChecksum(const std::vector<Record>& records,
                          const std::string& filename)
{
    for (const auto& r : records) {
        if (r.filename == filename) {
            std::cout << r.hash << "\n";
            return;
        }
    }
    std::cout << "File not found\n";
}

//entrypoint
int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cerr <<
          "Usage:\n"
          "  index <root> [workers]\n"
          "  find <root> <MB>\n"
          "  checksum <root> <filename>\n";
        return 1;
    }

    std::string mode = argv[1];
    fs::path root = argv[2];

    int workers = (argc >= 4 && mode == "index")
                    ? std::stoi(argv[3])
                    : 4;

    auto records = indexDirectory(root, workers);

    if (mode == "find") {
        queryFind(records, std::stoull(argv[3]));
    }
    else if (mode == "checksum") {
        queryChecksum(records, argv[3]);
    }

    return 0;
}