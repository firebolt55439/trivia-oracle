#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <curl/curlbuild.h>
#include <tuple>
#include <sstream>
#include <map>
#include <thread>
#include <mutex>

const char* SETTINGS_FILENAME = "./settings.txt";

namespace Colors {
    const char* HEADER = "\033[95m";
    const char* OKBLUE = "\033[94m";
    const char* OKGREEN = "\033[92m";
    const char* WARNING = "\033[93m";
    const char* FAIL = "\033[91m";
    const char* ENDC = "\033[0m";
    const char* BOLD = "\033[1m";
    const char* UNDERLINE = "\033[4m";
}

struct Settings {
    std::vector<std::string> filtered_words; // words to remove when searching
    std::vector<std::string> negative_words; // e.g. isn't, can't
};

Settings settings;
tesseract::TessBaseAPI* api;

struct Question {
    std::string question;
    std::vector<std::string> options;
    std::vector<double> scores;

    friend std::ostream& operator<<(std::ostream& outs, Question& q){
        outs << Colors::BOLD << Colors::UNDERLINE << q.question;
        outs << Colors::ENDC << std::endl << std::endl;
        const bool haveScores = !q.scores.empty();
        double highestScore = -1e99;
        unsigned int highestIdx = 0;
        if(haveScores){
            for(unsigned int i = 0; i < q.options.size(); i++){
                if(q.scores[i] > highestScore){
                    highestScore = q.scores[i];
                    highestIdx = i;
                }
            }
        }
        for(unsigned int i = 0; i < q.options.size(); i++){
            if(haveScores && i == highestIdx) outs << Colors::HEADER;
            outs << (i + 1) << ". " << q.options[i];
            if(haveScores){
                outs << " [score: " << Colors::BOLD << q.scores[i];
                if(i == highestIdx) outs << Colors::ENDC << Colors::HEADER;
                outs << "]" << Colors::ENDC;
            }
            outs << std::endl;
        }
        return outs << std::endl;
    }
};

std::string stripString(std::string s){
    // From back.
    while(!s.empty() && isspace(s.back())) s.pop_back();

    // From front.
    while(!s.empty() && isspace(s.front())) s.erase(0, 1);

    // Return result.
    return s;
}

std::string toLowerCase(std::string s){
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        ::tolower
    );
    return s;
}

size_t write_data(void *ptr, size_t size, size_t nmemb, void *stream){
    std::string data((const char*)ptr, (size_t)(size * nmemb));
    *((std::stringstream*) stream) << data << std::endl;
    return size * nmemb;
}

std::string downloadSearch(std::string term){
    void* curl = curl_easy_init();
    std::string url = "http://cors-fanfic-proxy.herokuapp.com/https://google.com/search?q=" + std::string(curl_easy_escape(curl, term.c_str(), 0));
    std::stringstream out;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "deflate");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_13_1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/62.0.3202.94 Safari/537.36");

    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK){
        std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << "\n";
        return "";
    }
    curl_easy_cleanup(curl);
    if(out.str().find("unusual traffic") != std::string::npos){
        std::cerr << "Appear to be blocked :(" << std::endl;
        std::cout << out.str();
        ::exit(1);
    }
    return out.str();
}

std::string filterWords(std::string question){
    // Break apart question into component words.
    std::vector<std::string> questionWords;
    std::string questionSearchPhrase = question;
    size_t last = 0, next = 0;
    while((next = questionSearchPhrase.find(" ", last)) != std::string::npos){
        questionWords.push_back(stripString(questionSearchPhrase.substr(last, next-last)));
        last = next + 1;
    }
    questionWords.push_back(stripString(questionSearchPhrase.substr(last)));

    // Filter words accordingly.
    size_t curPos = 0;
    for(auto& removingWord : settings.filtered_words){
        for(auto it = questionWords.rbegin(); it != questionWords.rend(); it++){
            if(toLowerCase(*it) == removingWord) it->erase();
            else if(!isalnum(removingWord[0]) && removingWord.length() == 1){
                if((curPos = (*it).find(removingWord)) != std::string::npos){
                    (*it).erase(curPos, 1);
                }
            }
        }
    }

    // Recombine words to produce appropriate search phrase.
    questionSearchPhrase = "";
    for(auto& s : questionWords){
        if(stripString(s).length() == 0) continue;
        questionSearchPhrase += s + " ";
    }
    questionSearchPhrase = stripString(questionSearchPhrase);
    return questionSearchPhrase;
}

unsigned int countOccurrences(std::string in, std::string of){
    size_t last = 0, next = 0;
    unsigned int num = 0;
    while((next = in.find(of, last)) != std::string::npos){
        ++num;
        last = next + 1;
    }
    return num;
}

unsigned int predictQuestionAnswer(Question q){
    // Filter words.
    std::string questionSearchPhrase = filterWords(q.question);
    for(unsigned int i = 0; i < q.options.size(); i++){
        q.options[i] = filterWords(q.options[i]);
    }
    printf("Query search phrase: %s\n", questionSearchPhrase.c_str());

    // Check for negative words.
    bool negative = false;
    const std::string lowerCaseQ = toLowerCase(q.question);
    for(auto& negativeWord : settings.negative_words){
        size_t foundPos = 0;
        if((foundPos = lowerCaseQ.find(negativeWord + " ")) != std::string::npos){
            bool is_quoted = countOccurrences(lowerCaseQ.substr(0, foundPos), "\"") % 2 != 0;
            if(!is_quoted) negative = true;
            break;
        }
    }

    // Define search string generator functions.
    std::string (*searchStringFn [])(std::string, std::string) = {
        [](std::string s, std::string opt){
            return s;
        },
        [](std::string s, std::string opt){
            return "\"" + s + "\" AND \"" + opt + "\"";
        }
    };

    // Define search string result processing functions.
    double (*searchProcessFn [])(std::string, std::string) = {
        [](std::string res, std::string opt){
            // Number of substring occurrences.
            size_t last = 0, next = 0;
            res = toLowerCase(res);
            opt = toLowerCase(opt);
            double score = 0.0;
            while((next = res.find(opt, last)) != std::string::npos){
                ++score;
                last = next + 1;
            }
            return score;
        },
        [](std::string res, std::string opt){
            // Number of search results.
            size_t pos;
            const std::string delim = "id=\"resultStats\">About ";
            if((pos = res.find(delim)) == std::string::npos) return 0.0;
            res = res.substr(pos + delim.length());
            res = res.substr(0, res.find(" "));
            res.erase(std::remove_if(res.begin(), res.end(), [](char c){
                return c == ',';
            }), res.end());
            double ret = atof(res.c_str());
            //printf("for |%s|, |%f| results\n", opt.c_str(), ret);
            return ret;
        }
    };

    // Search for each generated string per option in parallel.
    std::mutex downloadMutex;
    std::vector<std::tuple<std::string, std::string, unsigned int> > searchTerms;
    std::vector<std::thread> workers;
    for(auto opt : q.options){
        for(unsigned int i = 0; i < sizeof(searchStringFn) / sizeof(char*); i++){
            std::string term = (searchStringFn[i])(questionSearchPhrase, opt);
            workers.push_back(std::thread([term, i, &searchTerms, opt, &downloadMutex](){
                std::string res = downloadSearch(term);
                std::lock_guard<std::mutex> lk(downloadMutex);
                searchTerms.emplace_back(res, opt, i);
                printf("Option: '%s' | method: #%u | search term: '%s'\n", opt.c_str(), i + 1, term.c_str());
            }));
        }
    }
    std::for_each(workers.begin(), workers.end(), [](std::thread &t){
        t.join();
    });

    // Score each result.
    std::map<unsigned int, double> methodSum;
    std::vector<std::tuple<std::string, double, unsigned int> > scores;
    for(auto& resTup : searchTerms){
        auto res = std::get<0>(resTup);
        auto opt = std::get<1>(resTup);
        auto i = std::get<2>(resTup);
        double score = (searchProcessFn[i])(res, opt);
        if(methodSum.find(i) == methodSum.end()){
            methodSum[i] = 0.0;
        }
        methodSum[i] += score;
        scores.emplace_back(opt, score, i);
    }

    // Scale score for each option.
    std::map<std::string, double> scoreMap;
    for(auto& tup : scores){
        auto opt = std::get<0>(tup);
        auto score = std::get<1>(tup);
        auto i = std::get<2>(tup);
        if((methodSum[i] - 0.0) > 0.5){
            score *= (100.0 / methodSum[i]); // scale to 100
        }
        if(negative) score = -score;
        if(scoreMap.find(opt) == scoreMap.end()){
            scoreMap[opt] = 0.0;
        }
        scoreMap[opt] += score;
    }

    // Save scores.
    double highestScore = -1e99;
    int highestInd = -1, idx = 0;
    for(auto& opt : q.options){
        double on = scoreMap[opt];
        q.scores.push_back(on);
        if(on > highestScore){
            highestScore = on;
            highestInd = idx;
        }
        ++idx;
    }

    // Display result.
    std::cout << std::endl << q;

    // Return result.
    return highestInd;
}

struct TextBoundingBox {
    std::string line;
    float conf;
    int x1, y1, x2, y2;

    friend std::ostream& operator<<(std::ostream& outs, TextBoundingBox& tbb){
        outs << "line: '" << tbb.line << "' (conf: " << tbb.conf << "); box(";
        outs << tbb.x1 << "," << tbb.y1 << "," << tbb.x2 << "," << tbb.y2 << ")";
        return outs << std::endl;
    }
};

int recognizeQuestionFromImage(std::string filename, Question* q){
    Question ret;

    // Open input image with leptonica library
    Pix *image = pixRead(filename.c_str());
    api->SetImage(image);

    // Iterate over bounding boxes and confidence levels of recognized text.
    std::cout << "Processing image...";
    api->Recognize(0);
    std::cout << " [OK]\n";
    tesseract::ResultIterator* ri = api->GetIterator();
    tesseract::PageIteratorLevel level = tesseract::RIL_TEXTLINE;
    std::vector<TextBoundingBox> textboxes;
    if (ri != 0) {
        const float CONFIDENCE_THRESHOLD = 70.0;
        do {
            TextBoundingBox tbb;

            // Save recognized text.
            std::string line = stripString(ri->GetUTF8Text(level));
            tbb.line = line;
            if(line.find("left to reveal") != std::string::npos) continue;

            // Save confidence.
            float conf = ri->Confidence(level);
            tbb.conf = conf;

            // Create bounding box.
            int x1, y1, x2, y2;
            ri->BoundingBox(level, &x1, &y1, &x2, &y2);
            tbb.x1 = x1; tbb.y1 = y1;
            tbb.x2 = x2; tbb.y2 = y2;

            // Display textual representation.
            std::cout << tbb;
            if(conf < CONFIDENCE_THRESHOLD) continue;

            // Save object.
            textboxes.push_back(tbb);
        } while (ri->Next(level));
    }

    // Detect alignment of question and answers and use it to filter extranneous text.
    // First, sort by left bounding x values.
    std::vector<int> left_bounding_x;
    for(auto& tbb : textboxes){
        left_bounding_x.push_back(tbb.x1);
    }
    std::sort(left_bounding_x.begin(), left_bounding_x.end());

    // Next, detect largest left-biased clusters.
    int last_one = (int)-1e8, last_streak = 0;
    int largest_cluster = -1, largest_cluster_streak = -1;
    const int GROUPING_THRESHOLD = 60; // in pixels
    const int MAX_X_VALUE = 200;
    for(unsigned int i = 0; i < left_bounding_x.size();){
        auto& on = left_bounding_x[0];
        left_bounding_x.erase(left_bounding_x.begin());
        if(on > MAX_X_VALUE) continue;
        if(abs(on - last_one) < GROUPING_THRESHOLD){
            ++last_streak;
        } else {
            last_streak = 0;
        }
        last_one = on;
        if(last_streak > largest_cluster_streak){
            largest_cluster_streak = last_streak;
            largest_cluster = last_one;
        }
    }
    printf("largest cluster: %d (streak: %d)\n", largest_cluster, largest_cluster_streak);

    // Filter to area between topmost and bottommost clustered bounding boxes.
    std::vector<TextBoundingBox> filtered;
    int smallest_y = (int)1e8, largest_y = -1;
    for(auto& tbb : textboxes){
        if(abs(tbb.x1 - largest_cluster) >= GROUPING_THRESHOLD) continue;
        smallest_y = std::min(tbb.y1, smallest_y);
        largest_y = std::max(tbb.y1, largest_y);
    }
    std::copy_if(
        textboxes.begin(),
        textboxes.end(),
        std::back_inserter(filtered),
        [smallest_y, largest_y](auto a){
            return a.y1 >= smallest_y && a.y1 <= largest_y;
        }
    );
    printf("filtered:\n");
    for(auto& tbb : filtered){
        std::cout << tbb;
    }

    // Sanity check.
    const int MIN_BBS_FOR_SUCCESS = 4;
    if(filtered.size() < MIN_BBS_FOR_SUCCESS){
        pixDestroy(&image);
        return 1;
    }

    // Separate response options and question.
    const int NUM_OPTIONS = 3;
    unsigned int first_option_idx = filtered.size() - NUM_OPTIONS;
    for(unsigned int i = first_option_idx; i < filtered.size(); i++){
        ret.options.push_back(filtered[i].line);
    }
    ret.question = "";
    for(unsigned int i = 0; i < first_option_idx; i++){
        ret.question += filtered[i].line;
        if((i + 1) < first_option_idx) ret.question += " ";
    }

    // Display result.
    std::cout << std::endl << "Successfully recognized question." << std::endl;
    std::cout << ret;

    // Destroy used object
    pixDestroy(&image);

    // Fill in detected question and return success.
    *q = ret;
    return 0;
}

int main(int argc, char** argv){
    if(argc == 1){
        // TODO: Proper argparse
        fprintf(stderr, "No filename provided.\n");
        return 1;
    }

    // Initialize settings.
    curl_global_init(CURL_GLOBAL_ALL);
    std::ifstream ifp(SETTINGS_FILENAME);
    std::string cur_line;
    std::vector<std::string>* settingsPointerArr[] = {
        &settings.filtered_words,
        &settings.negative_words
    };
    int settingsPosition = -1;
    while(std::getline(ifp, cur_line)){
        if(cur_line == "[filtered words]") settingsPosition = 0;
        else if(cur_line == "[negative words]") settingsPosition = 1;
        else if(cur_line.length() > 0) settingsPointerArr[settingsPosition]->push_back(cur_line);
    }

    // Initialize tesseract-ocr with English, without specifying tessdata path
    std::cout << "Initializing tesseract library...";
    api = new tesseract::TessBaseAPI();
    if (api->Init(NULL, "eng")) {
        fprintf(stderr, "Could not initialize tesseract.\n");
        return 1;
    }
    std::cout << " [OK]\n";

    std::string mainArg(argv[1]);
    if(mainArg == "--live"){
        char c;
        while((c = getchar() != EOF)){
            // Capture the screenshot.
            system("screencapture -R0,22,490,855 tmp.png");

            // Recognize question from provided filename.
            Question q;
            int err;
            if((err = recognizeQuestionFromImage("tmp.png", &q))){
                std::cerr << "Could not recognize question.\n";
                continue;
            }

            // Predict question answer.
            predictQuestionAnswer(q);
        }
    } else {
        // Recognize question from provided filename.
        Question q;
        int err;
        if((err = recognizeQuestionFromImage(argv[1], &q))){
            std::cerr << "Could not recognize question.\n";
            return 1;
        }

        // Predict question answer.
        predictQuestionAnswer(q);
    }

    // Release memory.
    api->End();
    return 0;
}
