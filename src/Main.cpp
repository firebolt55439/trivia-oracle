#include <iostream>
#include <string>
#include <vector>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>

tesseract::TessBaseAPI* api;

struct Question {
    std::string question;
    std::vector<const char*> options;
};

Question recognizeQuestionFromImage(std::string filename){
    Question ret;

    // Open input image with leptonica library
    Pix *image = pixRead(filename.c_str());
    api->SetImage(image);

    /*
    // Get OCR result
    char* outText = api->GetUTF8Text();
    printf("OCR output:\n%s", outText);
    */

    // Iterate over bounding boxes and confidence levels of recognized text.
    std::cout << "Processing image...";
    api->Recognize(0);
    std::cout << " [OK]\n";
    tesseract::ResultIterator* ri = api->GetIterator();
    tesseract::PageIteratorLevel level = tesseract::RIL_TEXTLINE;
    if (ri != 0) {
        do {
          const char* word = ri->GetUTF8Text(level);
          float conf = ri->Confidence(level);
          int x1, y1, x2, y2;
          ri->BoundingBox(level, &x1, &y1, &x2, &y2);
          printf("word: '%s';  \tconf: %.2f; BoundingBox: %d,%d,%d,%d;\n",
                   word, conf, x1, y1, x2, y2);
          delete[] word;
        } while (ri->Next(level));
    }

    // Destroy used object
    pixDestroy(&image);

    // Return detected question.
    return ret;
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

    // Recognize question from image.
    Question q = recognizeQuestionFromImage(argv[1]);

    // Release memory.
    api->End();
    return 0;
}
