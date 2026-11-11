#include <iostream>
#include <fstream>
#include <string>
#include <queue>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <chrono>

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

    void add(long long ts, const string& page) {
        event_queue.push({ts, page});
        counts[page]++;
    }

    void advance(long long current_time) {
        long long cutoff = current_time - window;
        while (!event_queue.empty() && event_queue.front().ts <= cutoff) {
            string page = event_queue.front().page;
            event_queue.pop();
            
            counts[page]--;
            if (counts[page] == 0) {
                counts.erase(page);
            }
        }
    }

    // Optimized Top K using partial_sort (O(U log K))
    vector<pair<string, int>> top_k(int k) {
        vector<pair<string, int>> ranked(counts.begin(), counts.end());
        int limit = min((int)ranked.size(), k);
        
        partial_sort(ranked.begin(), ranked.begin() + limit, ranked.end(), 
            [](const pair<string, int>& a, const pair<string, int>& b) {
                if (a.second != b.second) return a.second > b.second;
                return a.first < b.first;
            });

        ranked.resize(limit);
        return ranked;
    }

    // Get rank of a specific page (O(U))
    int get_rank(const string& target_page) {
        auto it = counts.find(target_page);
        if (it == counts.end()) return -1;

        int target_count = it->second;
        int rank = 1;

        for (const auto& item : counts) {
            if (item.second > target_count || (item.second == target_count && item.first < target_page)) {
                rank++;
            }
        }
        return rank;
    }
};

int main() {
    // 600 seconds = 10 minute window
    FastEngine engine(600); 
    
    // Windows Path: Assuming you run this from the VS Code root workspace
    ifstream file("C:/tending/Trending_engine/data/events.tsv");
    
    if (!file.is_open()) {
        cout << "Error: Could not open file. Check your terminal directory and Windows file path!" << endl;
        return 1;
    }

    string line;
    long long current_ts = 0;

    cout << "Ingesting Real Data TSV firehose..." << endl;

    // Start the high-resolution timer
    auto start_time = chrono::high_resolution_clock::now();

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

    // Get the top 10 after ingesting the file
    auto trending = engine.top_k(10);
    
    // Stop the timer
    auto end_time = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed = end_time - start_time;

    // Output results
    cout << "\nTop 10 Trending Pages at timestamp " << current_ts << ":" << endl;
    for (const auto& item : trending) {
        cout << item.second << " edits\t" << item.first << endl;
    }

    cout << "\n-----------------------------------" << endl;
    cout << "C++ Internal Execution Time: " << elapsed.count() << " seconds" << endl;
    cout << "-----------------------------------" << endl;

    return 0;
}