#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

tesseract::TessBaseAPI* api;

struct Question {
    std::string question;
    std::vector<std::string> options;

    friend std::ostream& operator<<(std::ostream& outs, Question& q){
        outs << "question: '" << q.question << "'; options: [";
        for(unsigned int i = 0; i < q.options.size(); i++){
            outs << "'" << q.options[i] << "'";
            if((i + 1) < q.options.size()) outs << ", ";
        }
        outs << "]";
        return outs << std::endl;
    }
};

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
        do {
            TextBoundingBox tbb;

            // Save recognized text.
            std::string line = ri->GetUTF8Text(level);
            while(isspace(line.back())) line.pop_back();
            tbb.line = line;

            // Save confidence.
            float conf = ri->Confidence(level);
            tbb.conf = conf;

            // Save bounding box.
            int x1, y1, x2, y2;
            ri->BoundingBox(level, &x1, &y1, &x2, &y2);
            tbb.x1 = x1; tbb.y1 = y1;
            tbb.x2 = x2; tbb.y2 = y2;
            textboxes.push_back(tbb);

            // Display textual representation.
            std::cout << tbb;
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

    // Initialize tesseract-ocr with English, without specifying tessdata path
    std::cout << "Initializing tesseract library...";
    api = new tesseract::TessBaseAPI();
    if (api->Init(NULL, "eng")) {
        fprintf(stderr, "Could not initialize tesseract.\n");
        return 1;
    }
    std::cout << " [OK]\n";

    // Recognize question from provided filename.
    Question q;
    int err;
    if((err = recognizeQuestionFromImage(argv[1], &q))){
        std::cerr << "Could not recognize question.\n";
        return 1;
    }

    // Release memory.
    api->End();
    return 0;
}
