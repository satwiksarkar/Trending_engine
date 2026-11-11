
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <queue>
#include <unordered_map>
#include <vector>
#include <algorithm>

using namespace std;

struct Event {
    long long ts;
    string page;
};

class FastEngine {
private:
    long long window;
    queue<Event> event_queue;
    unordered_map<string, int> counts;

public:
    FastEngine(long long window_size) : window(window_size) {}

    // Add a new event
    void add(long long ts, const string& page) {
        event_queue.push({ts, page});
        counts[page]++;
    }

    // Remove events outside the sliding window
    void advance(long long current_time) {
        long long cutoff = current_time - window;

        while (!event_queue.empty() &&
               event_queue.front().ts <= cutoff) {

            string page = event_queue.front().page;
            event_queue.pop();

            counts[page]--;

            if (counts[page] == 0) {
                counts.erase(page);
            }
        }
    }

    // Get Top K trending pages
    vector<pair<string, int>> top_k(int k) {
        vector<pair<string, int>> ranked(
            counts.begin(), counts.end()
        );

        // Sort by count descending, then alphabetically
        sort(ranked.begin(), ranked.end(),
             [](const pair<string, int>& a,
                const pair<string, int>& b) {

                 if (a.second != b.second)
                     return a.second > b.second;

                 return a.first < b.first;
             });

        if (ranked.size() > k) {
            ranked.resize(k);
        }

        return ranked;
    }
};

int main() {
    // 600 seconds = 10-minute sliding window
    FastEngine engine(600);

    ifstream file("../data/events.tsv");

    if (!file.is_open()) {
        cerr << "Error: Could not open events.tsv" << endl;
        return 1;
    }

    string line;
    long long current_ts = 0;

    cout << "Ingesting TSV firehose..." << endl;

    while (getline(file, line)) {
        size_t tab_pos = line.find('\t');

        if (tab_pos != string::npos) {
            long long ts = stoll(line.substr(0, tab_pos));
            string page = line.substr(tab_pos + 1);

            engine.add(ts, page);
            engine.advance(ts);

            current_ts = ts;
        }
    }

    file.close();

    // Print the Top 10 trending pages
    auto trending = engine.top_k(10);

    cout << "\nTop 10 Trending Pages at timestamp "
         << current_ts << ":" << endl;

    for (const auto& item : trending) {
        cout << item.second << " edits\t"
             << item.first << endl;
    }

    return 0;
}
