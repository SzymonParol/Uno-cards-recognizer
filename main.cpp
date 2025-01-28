#include <iostream>
#include <opencv2/opencv.hpp>

using namespace std;
using namespace cv;

const float CARD_WIDTH = 300, CARD_HEIGHT = 400; // Width and height of the scaled card
Point2f DESTINATION_POINTS[4] = { {0.0f, 0.0f}, {CARD_WIDTH, 0.0f}, {CARD_WIDTH, CARD_HEIGHT}, {0.0f, CARD_HEIGHT} }; // Destination points for the scaled card

bool isBlurred(Mat& grayImage, Mat& laplacian) {
    Laplacian(grayImage, laplacian, CV_32FC1);
    Scalar mean, stddev;
    meanStdDev(laplacian, mean, stddev, grayImage);
    double variance = stddev.val[0] * stddev.val[0];
    const double BLUR_THRESHOLD = 10;
    return variance <= BLUR_THRESHOLD;
}

void preprocessImage(Mat& image, Mat& cannyOutput, Mat& grayImage) {
    Mat temp, laplacian;
    cvtColor(image, grayImage, COLOR_BGR2GRAY);

    if (isBlurred(grayImage, laplacian)) {
        const double ALPHA = 4;
        for (int i = 0; i < 3; i++) {
            GaussianBlur(grayImage, temp, Size(301, 301), 2.0, 2.0);
            addWeighted(grayImage, 1 + ALPHA, temp, -ALPHA, 0, grayImage);
        }
        medianBlur(grayImage, grayImage, 5);
        Canny(grayImage, cannyOutput, 140, 255);
    }
    else {
        medianBlur(grayImage, grayImage, 7);
        Canny(grayImage, cannyOutput, 100, 200);
    }
}

void findContoursAndWarp(Mat& image, Mat& grayImage, Mat& cannyOutput, vector<Mat>& warpedImages, vector<RotatedRect>& minRects, vector<Point2f>& cardCenters) {
    vector<vector<Point>> contours;
    vector<Vec4i> hierarchy;
    Mat kernel = getStructuringElement(MORPH_RECT, Size(3, 3));
    dilate(cannyOutput, cannyOutput, kernel);
    dilate(cannyOutput, cannyOutput, kernel);

    findContours(cannyOutput, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    vector<RotatedRect> minRectsTemp(contours.size());
    Point2f sourcePoints[4];

    for (size_t i = 0; i < contours.size(); i++) {
        if (contourArea(contours[i]) >= 300000) {
            minRectsTemp[i] = minAreaRect(contours[i]);
            Point2f rectPoints[4];
            minRectsTemp[i].points(rectPoints);

            for (int j = 0; j < 4; j++) {
                line(image, rectPoints[j], rectPoints[(j + 1) % 4], Scalar(0, 125, 255), 15);
            }

            if (norm(rectPoints[0] - rectPoints[1]) < norm(rectPoints[0] - rectPoints[3])) {
                for (int j = 0; j < 4; j++) {
                    sourcePoints[j] = rectPoints[j];
                }
            }
            else {
                for (int j = 0; j < 4; j++) {
                    sourcePoints[j] = rectPoints[(j + 3) % 4];
                }
            }

            Mat transformMatrix = getPerspectiveTransform(sourcePoints, DESTINATION_POINTS);
            Mat warpedImage;
            warpPerspective(image, warpedImage, transformMatrix, Point(CARD_WIDTH, CARD_HEIGHT));
            warpedImages.push_back(warpedImage);

            // Calculate the center of the card
            Point2f center = (rectPoints[0] + rectPoints[1] + rectPoints[2] + rectPoints[3]) / 4;
            cardCenters.push_back(center);
        }
    }

    minRects = minRectsTemp;
}

void applyCardMask(Mat& card) {
    Point center = { 70, 65 };
    Mat mask = Mat::zeros(CARD_HEIGHT, CARD_WIDTH, CV_8UC3);
    circle(mask, center, 45, Scalar(255, 255, 255), -1);
    bitwise_and(card, mask, card);
}

string determineSymbol(float mom2, float mom3) {
    if (((mom2 <= 5.1) || (mom2 >= 7.35 && mom2 <= 7.6)) && ((mom3 >= 7 && mom3 <= 7.1) || (mom3 > 11.5 && mom3 < 14.2))) return "0";
    if (((mom2 > 5.65 && mom2 < 5.8) || (mom2 > 5.9 && mom2 < 6.66)) && ((mom3 > 8.3 && mom3 < 8.4) || (mom3 > 8.6 && mom3 < 9.35) || (mom3 > 9.36 && mom3 < 9.7))) return "1";
    if (((mom2 > 5.8 && mom2 < 5.9) || (mom2 > 7.25 && mom2 < 7.74)) && ((mom3 > 8.5 && mom3 < 8.7) || (mom3 > 10.5 && mom3 < 11.2))) return "8";
    if ((mom2 > 5.9 && mom2 < 6.36) && ((mom3 > 8.4 && mom3 < 8.6) || (mom3 > 11.6 && mom3 < 13.1))) return "Reverse";
    return "Stop";
}

string computeMoments(Mat& image, string& symbol) {
    Mat grayImage;
    cvtColor(image, grayImage, COLOR_BGR2GRAY);
    int median = 0;
    for (int i = 0; i < grayImage.rows; i++) {
        for (int j = 0; j < grayImage.cols; j++) {
            median += grayImage.at<uchar>(i, j);
        }
    }
    median = median / (grayImage.rows * grayImage.cols) + 40;
    medianBlur(grayImage, grayImage, 7);
    threshold(grayImage, grayImage, median, 255, THRESH_BINARY);
    medianBlur(grayImage, grayImage, 5);

    Moments imageMoments = moments(grayImage, false);
    double huMoments[7];
    HuMoments(imageMoments, huMoments);
    for (int i = 0; i < 7; i++) {
        huMoments[i] = -1 * copysign(1.0, huMoments[i]) * log10(abs(huMoments[i]));
    }

    return determineSymbol(huMoments[1], huMoments[2]);
}

void postProcessCard(Mat& image, Mat& card, string& color, string& symbol) {
    double alpha = 4;
    double ch1 = 0, ch2 = 0, ch3 = 0;
    Mat temp;
    const int RANGE = 150;

    if (isBlurred(image, temp)) {
        for (int i = 0; i < 3; i++) {
            GaussianBlur(card, temp, Size(3, 3), 1, 1);
            addWeighted(card, 1 + alpha, temp, -alpha, 0, card);
        }
    }

    threshold(card, temp, 90, 255, THRESH_BINARY);
    for (int i = 0; i < temp.rows; i++) {
        for (int j = 0; j < temp.cols; j++) {
            ch1 += temp.at<Vec3b>(i, j)[0];
            ch2 += temp.at<Vec3b>(i, j)[1];
            ch3 += temp.at<Vec3b>(i, j)[2];
        }
    }

    alpha = temp.rows * temp.cols;
    ch1 /= alpha;
    ch2 /= alpha;
    ch3 /= alpha;

    if (ch1 >= RANGE) color = "Blue";
    else if (ch2 >= RANGE) color = (ch3 >= RANGE) ? "Yellow" : "Green";
    else color = "Red";

    symbol = computeMoments(card, symbol);
}

void drawLabels(Mat& image, vector<Point2f>& cardCenters, vector<string>& colors, vector<string>& symbols) {
    for (size_t i = 0; i < cardCenters.size(); i++) {
        string label = colors[i] + " " + symbols[i];
        putText(image, label, cardCenters[i], FONT_HERSHEY_SIMPLEX, 2, Scalar(0, 255, 0), 3);
    }
}

int main() {
    string imagePath = "resources/1.png";
    Mat originalImage = imread(imagePath);
    vector<Mat> warpedImages;
    Mat cannyOutput, grayImage;
    vector<RotatedRect> minRects;
    vector<Point2f> cardCenters;
    vector<string> colors = { "color", "color", "color", "color" };
    vector<string> symbols = { "symbol", "symbol", "symbol", "symbol" };

    preprocessImage(originalImage, cannyOutput, grayImage);
    findContoursAndWarp(originalImage, grayImage, cannyOutput, warpedImages, minRects, cardCenters);

    Mat cards[4] = { warpedImages[0].clone(), warpedImages[1].clone(), warpedImages[2].clone(), warpedImages[3].clone() };
    for (int i = 0; i < 4; i++) applyCardMask(warpedImages[i]);

    Rect roi(30, 30, 80, 80);
    Mat croppedImages[4];
    for (int i = 0; i < 4; i++) croppedImages[i] = warpedImages[i](roi);

    cvtColor(originalImage, grayImage, COLOR_BGR2GRAY);
    for (int i = 0; i < 4; i++) postProcessCard(grayImage, croppedImages[i], colors[i], symbols[i]);


    drawLabels(originalImage, cardCenters, colors, symbols);
    cv::resize(originalImage, originalImage, cv::Size(1080, 640), 0, 0);
    imshow("Detected Cards", originalImage);
    imwrite("resources/3_detected.jpg", originalImage);
    waitKey(0);

    return 0;
}