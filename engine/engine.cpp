#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

struct Event {
    long long ts; // Unix time in seconds
    string page;
};

// priority_queue is a max-heap by default. This comparator makes it a min-heap,
// so the event with the earliest timestamp is always at the top.
struct EarlierEvent {
    bool operator()(const Event& a, const Event& b) const {
        return a.ts > b.ts;
    }
};

// Convert a Gregorian calendar date to days since 1970-01-01.
// This avoids platform-specific functions such as timegm().
static long long days_from_civil(int year, unsigned month, unsigned day) {
    year -= (month <= 2);
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
    const unsigned adjusted_month = month > 2 ? month - 3 : month + 9;
    const unsigned day_of_year =
        (153 * adjusted_month + 2) / 5 + day - 1;
    const unsigned day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    return static_cast<long long>(era) * 146097LL + day_of_era - 719468LL;
}

static bool all_digits(const string& s, size_t begin, size_t length) {
    if (begin + length > s.size()) return false;
    for (size_t i = begin; i < begin + length; ++i) {
        if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

static int number_at(const string& s, size_t pos, size_t length) {
    return stoi(s.substr(pos, length));
}

// Accepts either Unix epoch seconds (e.g. 1794416714) or UTC ISO-8601
// timestamps (e.g. 2026-11-11T15:14:52.257Z). Fractional seconds are
// validated but truncated because the engine's window is measured in seconds.
static bool parse_timestamp(const string& input, long long& result) {
    if (input.empty()) return false;

    // Unix epoch timestamp.
    if (all_digits(input, 0, input.size())) {
        try {
            size_t used = 0;
            result = stoll(input, &used);
            return used == input.size();
        } catch (...) {
            return false;
        }
    }

    // ISO-8601 UTC timestamp: YYYY-MM-DDTHH:MM:SS[.fraction]Z
    if (input.size() < 20 || input[4] != '-' || input[7] != '-' ||
        input[10] != 'T' || input[13] != ':' || input[16] != ':') {
        return false;
    }

    if (!all_digits(input, 0, 4) || !all_digits(input, 5, 2) ||
        !all_digits(input, 8, 2) || !all_digits(input, 11, 2) ||
        !all_digits(input, 14, 2) || !all_digits(input, 17, 2)) {
        return false;
    }

    const int year = number_at(input, 0, 4);
    const int month = number_at(input, 5, 2);
    const int day = number_at(input, 8, 2);
    const int hour = number_at(input, 11, 2);
    const int minute = number_at(input, 14, 2);
    const int second = number_at(input, 17, 2);

    if (month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59)
        return false;

    static const int month_days[] =
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int max_day = month_days[month - 1];
    const bool leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
    if (month == 2 && leap_year) max_day = 29;
    if (day < 1 || day > max_day) return false;

    // Require UTC 'Z'. Fractional seconds, if present, must contain digits.
    size_t pos = 19;
    if (pos < input.size() && input[pos] == '.') {
        ++pos;
        const size_t fraction_start = pos;
        while (pos < input.size() && isdigit(static_cast<unsigned char>(input[pos])))
            ++pos;
        if (pos == fraction_start) return false;
    }
    if (pos != input.size() - 1 || input[pos] != 'Z') return false;

    const long long days = days_from_civil(year, static_cast<unsigned>(month),
                                           static_cast<unsigned>(day));
    result = days * 86400LL + hour * 3600LL + minute * 60LL + second;
    return true;
}

class FastEngine {
private:
    long long window_seconds;
    priority_queue<Event, vector<Event>, EarlierEvent> expiry_queue;
    unordered_map<string, long long> counts;

public:
    explicit FastEngine(long long window_size_seconds)
        : window_seconds(window_size_seconds) {
        if (window_seconds <= 0) {
            throw invalid_argument("Window size must be positive.");
        }
    }

    // Add an event. The event may arrive slightly out of timestamp order;
    // the min-heap keeps expiration correct in that case.
    void add(long long timestamp, const string& page) {
        if (page.empty()) return;
        expiry_queue.push(Event{timestamp, page});
        ++counts[page];
    }

    // Remove every event whose timestamp is at or before the window cutoff.
    // Active window semantics: (current_time - window_seconds, current_time].
    void advance(long long current_time) {
        const long long cutoff = current_time - window_seconds;

        while (!expiry_queue.empty() && expiry_queue.top().ts <= cutoff) {
            const string& page = expiry_queue.top().page;
            auto it = counts.find(page);

            if (it != counts.end()) {
                --(it->second);
                if (it->second == 0) counts.erase(it);
            }
            expiry_queue.pop();
        }
    }

    // Return at most K pages, sorted by count descending, then page key ascending.
    // Average complexity: O(U log K), where U is the number of active unique pages.
    vector<pair<string, long long>> top_k(int k) const {
        if (k <= 0 || counts.empty()) return {};

        vector<pair<string, long long>> ranked(counts.begin(), counts.end());
        const size_t limit = min(ranked.size(), static_cast<size_t>(k));

        partial_sort(
            ranked.begin(), ranked.begin() + limit, ranked.end(),
            [](const auto& a, const auto& b) {
                if (a.second != b.second) return a.second > b.second;
                return a.first < b.first;
            }
        );

        ranked.resize(limit);
        return ranked;
    }

    // 1-based rank using the same tie-breaking rule as top_k(). Returns -1
    // when the page is not currently active. Complexity: O(U) average.
    long long get_rank(const string& target_page) const {
        auto target = counts.find(target_page);
        if (target == counts.end()) return -1;

        const long long target_count = target->second;
        long long rank = 1;

        for (const auto& item : counts) {
            if (item.second > target_count ||
                (item.second == target_count && item.first < target_page)) {
                ++rank;
            }
        }
        return rank;
    }
};

int main(int argc, char* argv[]) {
    // Defaults assume the program is run from the engine/ directory.
    const string input_path = (argc >= 2) ? argv[1] : "../data/events.tsv";
    const long long window_seconds = (argc >= 3) ? stoll(argv[2]) : 600;
    const int top_k_count = (argc >= 4) ? stoi(argv[3]) : 10;

    if (window_seconds <= 0 || top_k_count < 0) {
        cerr << "Error: window must be positive and Top-K cannot be negative.\n";
        return 1;
    }

    ifstream file(input_path);
    if (!file.is_open()) {
        cerr << "Error: Could not open " << input_path << '\n'
             << "Run from the engine directory or pass the TSV path as the first argument.\n";
        return 1;
    }

    FastEngine engine(window_seconds);
    string line;
    long long current_time = 0;
    size_t valid_rows = 0;
    size_t skipped_rows = 0;

    cout << "Reading events from: " << input_path << '\n';
    cout << "Sliding window: " << window_seconds << " seconds\n";

    const auto start = chrono::steady_clock::now();

    while (getline(file, line)) {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const size_t tab_pos = line.find('\t');
        if (tab_pos == string::npos || tab_pos + 1 >= line.size()) {
            ++skipped_rows;
            continue;
        }

        const string timestamp_text = line.substr(0, tab_pos);
        const string page = line.substr(tab_pos + 1);
        long long timestamp = 0;

        if (!parse_timestamp(timestamp_text, timestamp) || page.empty()) {
            ++skipped_rows;
            continue;
        }

        engine.add(timestamp, page);
        // Use the maximum timestamp seen so far. This handles small timestamp
        // reordering in the TSV without moving the window backwards.
        if (valid_rows == 0 || timestamp > current_time)
            current_time = timestamp;
        engine.advance(current_time);
        ++valid_rows;
    }

    const auto trending = engine.top_k(top_k_count);
    const auto end = chrono::steady_clock::now();
    const chrono::duration<double> elapsed = end - start;

    cout << "\nTop " << top_k_count << " trending pages at Unix timestamp "
         << current_time << ":\n";
    if (trending.empty()) cout << "No active pages in the current window.\n";

    for (size_t i = 0; i < trending.size(); ++i) {
        cout << (i + 1) << ". " << trending[i].second << " edits\t"
             << trending[i].first << '\n';
    }

    cout << "\nRows processed: " << valid_rows
         << " | Rows skipped: " << skipped_rows << '\n';
    cout << "Processing time (ingestion + expiry + Top-K): "
         << elapsed.count() << " seconds\n";

    // Example individual-rank query (uncomment and replace the page key):
    // cout << "Rank: " << engine.get_rank("enwiki|Artificial_intelligence") << '\n';

    return 0;
}
