// #include <algorithm>
// #include <chrono>
// #include <deque>
// #include <filesystem>
// #include <fstream>
// #include <iostream>
// #include <optional>
// #include <string>
// #include <vector>

// namespace fs = std::filesystem;

// // -----------------------------
// // Job model (students may extend)
// // -----------------------------
// struct Job {
//     fs::path path;

//     // "arrival time" in ticks (simple counter in this simulation)
//     int arrival = 0;

//     // estimated cost (used for SJF/MLFQ ideas)
//     int est_cost = 1;

//     // for RR/MLFQ: remaining "work units" (simple abstraction)
//     int remaining = 1;

//     // for MLQ/MLFQ: which queue the job is in
//     int queue_level = 0;
// };

// // -----------------------------
// // Small helpers
// // -----------------------------
// static std::string jsonEscape(const std::string& s) {
//     // Minimal JSON string escape (good enough for file paths/names)
//     std::string out;
//     out.reserve(s.size() + 8);
//     for (char c : s) {
//         switch (c) {
//             case '\\': out += "\\\\"; break;
//             case '"':  out += "\\\""; break;
//             case '\n': out += "\\n"; break;
//             case '\r': out += "\\r"; break;
//             case '\t': out += "\\t"; break;
//             default:   out += c; break;
//         }
//     }
//     return out;
// }

// static std::string permsToString(fs::perms p) {
//     // POSIX-like rwx string, best-effort cross-platform
//     auto bit = [&](fs::perms b) { return (p & b) != fs::perms::none; };

//     std::string s;
//     s += bit(fs::perms::owner_read)  ? 'r' : '-';
//     s += bit(fs::perms::owner_write) ? 'w' : '-';
//     s += bit(fs::perms::owner_exec)  ? 'x' : '-';

//     s += bit(fs::perms::group_read)  ? 'r' : '-';
//     s += bit(fs::perms::group_write) ? 'w' : '-';
//     s += bit(fs::perms::group_exec)  ? 'x' : '-';

//     s += bit(fs::perms::others_read)  ? 'r' : '-';
//     s += bit(fs::perms::others_write) ? 'w' : '-';
//     s += bit(fs::perms::others_exec)  ? 'x' : '-';
//     return s;
// }

// // Convert filesystem::file_time_type to seconds since epoch (best-effort)
// static long long fileTimeToEpochSeconds(fs::file_time_type ft) {
//     // This conversion is a common approach for C++17; it’s “best-effort”
//     using namespace std::chrono;
//     auto sctp = time_point_cast<system_clock::duration>(
//         ft - fs::file_time_type::clock::now() + system_clock::now()
//     );
//     return duration_cast<seconds>(sctp.time_since_epoch()).count();
// }

// // -----------------------------
// // Indexing work (the "CPU burst")
// // -----------------------------
// static std::string scanOnePathJson(const fs::path& p) {
//     // Build ONE JSON object (as a string) representing the file record.
//     // Keep it simple: path, name, size, last_write_time, type flags, permissions, errors.
//     std::string pathStr = p.string();
//     std::string nameStr = p.filename().string();

//     bool is_file = false;
//     bool is_dir = false;
//     bool is_symlink = false;
//     unsigned long long size_bytes = 0;
//     long long mtime_epoch = 0;
//     std::string perms_rwx = "---------";
//     std::string error;

//     try {
//         // status() follows symlinks; symlink_status() does not
//         fs::file_status st = fs::symlink_status(p);

//         is_symlink = fs::is_symlink(st);
//         is_file = fs::is_regular_file(st);
//         is_dir = fs::is_directory(st);

//         // Size only valid for regular files
//         if (is_file) {
//             size_bytes = fs::file_size(p);
//         }

//         // Last write time
//         mtime_epoch = fileTimeToEpochSeconds(fs::last_write_time(p));

//         // Permissions
//         perms_rwx = permsToString(st.permissions());
//     }
//     catch (const fs::filesystem_error& e) {
//         error = e.what();
//     }
//     catch (const std::exception& e) {
//         error = e.what();
//     }

//     // Note: Full ACLs on Windows (SIDs/ACEs) require platform APIs / extra libs.
//     // This template records basic permission bits only.

//     std::string json = "{";
//     json += "\"path\":\"" + jsonEscape(pathStr) + "\",";
//     json += "\"name\":\"" + jsonEscape(nameStr) + "\",";
//     json += "\"is_file\":" + std::string(is_file ? "true" : "false") + ",";
//     json += "\"is_dir\":" + std::string(is_dir ? "true" : "false") + ",";
//     json += "\"is_symlink\":" + std::string(is_symlink ? "true" : "false") + ",";
//     json += "\"size_bytes\":" + std::to_string(size_bytes) + ",";
//     json += "\"mtime_epoch\":" + std::to_string(mtime_epoch) + ",";
//     json += "\"perms_rwx\":\"" + perms_rwx + "\"";

//     if (!error.empty()) {
//         json += ",\"error\":\"" + jsonEscape(error) + "\"";
//     }

//     json += "}";
//     return json;
// }

// // -----------------------------
// // Job creation (workload)
// // -----------------------------
// static std::vector<Job> buildJobs(const fs::path& root) {
//     std::vector<Job> jobs;
//     int tick = 0;

//     for (auto const& entry : fs::recursive_directory_iterator(root)) {
//         if (!entry.is_regular_file()) continue; // keep it simple: index files only

//         ++tick;
//         fs::path p = entry.path();

//         // Simple estimate: size buckets -> est_cost (1,2,3)
//         unsigned long long size_bytes = 0;
//         try {
//             size_bytes = entry.file_size();
//         } catch (...) {
//             size_bytes = 0;
//         }

//         int est = 1;
//         if (size_bytes >= 100000ULL && size_bytes < 10000000ULL) est = 2;   // 100KB..10MB
//         else if (size_bytes >= 10000000ULL) est = 3;                        // >=10MB

//         Job j;
//         j.path = p;
//         j.arrival = tick;
//         j.est_cost = est;
//         j.remaining = est;     // “work units” tied to cost
//         j.queue_level = 0;     // starts high for MLFQ ideas
//         jobs.push_back(j);
//     }

//     // Arrival order
//     std::sort(jobs.begin(), jobs.end(), [](const Job& a, const Job& b) {
//         return a.arrival < b.arrival;
//     });

//     return jobs;
// }

// // -----------------------------
// // Scheduler hooks (students adapt)
// // -----------------------------
// static std::optional<Job> chooseNextJob(std::deque<Job>& ready, int /*tick*/) {
//     /*
//       STUDENT TASK: Replace this logic to implement a scheduler.

//       Current behaviour: FCFS (pop from front)

//       Ideas:
//       - FCFS:         pop_front
//       - SJF:          choose job with smallest est_cost (remove it)
//       - Round Robin:  pop_front, run 1 unit, if remaining>0 push_back
//       - MLQ:          multiple queues by queue_level, always choose highest queue first
//       - MLFQ:         demote when it uses full quantum; boost occasionally
//     */
//     if (ready.empty()) return std::nullopt;

//     Job j = ready.front();
//     ready.pop_front();
//     return j;
// }

// static void onJobFeedback(Job& /*job*/, const std::string& /*jsonRecord*/) {
//     /*
//       Optional STUDENT TASK:
//       Use results to change scheduling behaviour (MLFQ-style).

//       Examples:
//       - If jsonRecord contains "error": demote job.queue_level
//       - If job.est_cost is high: demote
//       - If job finishes quickly: keep high priority
//     */
// }

// // -----------------------------
// // Simulation loop (runs "scheduler")
// // -----------------------------
// static void runIndexer(const fs::path& root, const fs::path& outputJsonl) {
//     std::vector<Job> jobs = buildJobs(root);

//     // In this simple model, all jobs are ready immediately.
//     std::deque<Job> ready(jobs.begin(), jobs.end());

//     std::ofstream out(outputJsonl);
//     if (!out) {
//         throw std::runtime_error("Could not open output file for writing.");
//     }

//     int tick = 0;

//     while (!ready.empty()) {
//         ++tick;

//         auto next = chooseNextJob(ready, tick);
//         if (!next.has_value()) continue;

//         Job job = next.value();

//         // "Run" job (index one file)
//         std::string record = scanOnePathJson(job.path);

//         // Add a few scheduling fields (simple: append before closing brace)
//         // (Teaching-friendly; students can make proper JSON building later.)
//         if (!record.empty() && record.back() == '}') {
//             record.pop_back();
//             record += ",\"arrival\":" + std::to_string(job.arrival);
//             record += ",\"est_cost\":" + std::to_string(job.est_cost);
//             record += ",\"queue_level\":" + std::to_string(job.queue_level);
//             record += ",\"tick_ran\":" + std::to_string(tick);
//             record += "}";
//         }

//         onJobFeedback(job, record);

//         out << record << "\n";
//     }

//     std::cout << "Done. Wrote: " << outputJsonl << "\n";
// }

// int main() {
//     try {
//         fs::path root = R"(C:\temp)";              // change me
//         fs::path out  = "index_results.jsonl";     // output file
//         runIndexer(root, out);
//     }
//     catch (const std::exception& e) {
//         std::cerr << "Fatal error: " << e.what() << "\n";
//         return 1;
//     }
//     return 0;
// }
