#include <iostream>
#include <iomanip>
#include <stdexcept>

#include <opencv2/objdetect/objdetect.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/gpu/gpu.hpp>

#include "utility.h"

using namespace std;
using namespace cv;
using namespace cv::gpu;

template<class T> void resizeAndConvert(const T& src, T& resized, T& gray, double scale)
{
    Size sz(cvRound(src.cols * scale), cvRound(src.rows * scale));

    if (scale != 1)
        resize(src, resized, sz);
    else
        resized = src;

    if (resized.channels() == 3)
        cvtColor(resized, gray, COLOR_BGR2GRAY);
    else if (resized.channels() == 4)
        cvtColor(resized, gray, COLOR_BGRA2GRAY);
    else
        gray = resized;
}

class App : public BaseApp
{
public:
    App();

protected:
    void process();
    bool processKey(int key);
    bool parseCmdArgs(int& i, int argc, const char* argv[]);
    void printHelp();

private:
    void displayState(cv::Mat& frame, double proc_fps, double total_fps);

    string cascade_name;
    CascadeClassifier_GPU cascade_gpu;
    CascadeClassifier cascade_cpu;

    bool useGPU;
    double scaleFactor;
    bool findLargestObject;
    bool filterRects;
    bool showHelp;

    size_t curSource;
};

App::App()
{
    useGPU = true;
    scaleFactor = 1.4;
    findLargestObject = false;
    filterRects = true;
    showHelp = false;
    curSource = 0;
}

void App::process()
{
    if (cascade_name.empty())
    {
        cout << "Using default cascade file..." << endl;
        cascade_name = "data/face_detect/haarcascade_frontalface_alt.xml";
    }

    if (!cascade_gpu.load(cascade_name) || !cascade_cpu.load(cascade_name))
        THROW_EXCEPTION("Could not load cascade classifier [" << cascade_name << "]");

    if (sources.empty())
    {
        cout << "Using default frames source..." << endl;
        sources.push_back(new VideoSource("data/face_detect/browser.flv"));
    }

    Mat frame, frame_cpu, gray_cpu, resized_cpu, img_to_show;
    GpuMat frame_gpu, gray_gpu, resized_gpu, facesBuf_gpu;

    vector<Rect> faces;

    while (!exited)
    {
        int64 start = getTickCount();

        sources[curSource]->next(frame_cpu);

        double proc_fps;
        if (useGPU)
        {
            frame_gpu.upload(frame_cpu);
            resizeAndConvert(frame_gpu, resized_gpu, gray_gpu, scaleFactor);

            cascade_gpu.visualizeInPlace = false;
            cascade_gpu.findLargestObject = findLargestObject;

            int64 proc_start = getTickCount();

            int detections_num = cascade_gpu.detectMultiScale(gray_gpu, facesBuf_gpu, 1.2, (filterRects || findLargestObject) ? 4 : 0);

            if (detections_num == 0)
                faces.clear();
            else
            {
                faces.resize(detections_num);
                Mat facesMat(1, detections_num, DataType<Rect>::type, &faces[0]);
                facesBuf_gpu.colRange(0, detections_num).download(facesMat);
            }

            proc_fps = getTickFrequency() / (getTickCount() - proc_start);
        }
        else
        {
            resizeAndConvert(frame_cpu, resized_cpu, gray_cpu, scaleFactor);

            Size minSize = cascade_gpu.getClassifierSize();

            int64 proc_start = getTickCount();

            cascade_cpu.detectMultiScale(gray_cpu, faces, 1.2,
                                         (filterRects || findLargestObject) ? 4 : 0,
                                         (findLargestObject ? CV_HAAR_FIND_BIGGEST_OBJECT : 0)
                                            | CV_HAAR_SCALE_IMAGE,
                                         minSize);

            proc_fps = getTickFrequency() / (getTickCount() - proc_start);
        }

        if (useGPU)
            resized_gpu.download(img_to_show);
        else
            img_to_show = resized_cpu;

        for (size_t i = 0; i < faces.size(); i++)
            rectangle(img_to_show, faces[i], CV_RGB(0, 255, 0), 3);

        double total_fps = getTickFrequency() / (getTickCount() - start);

        displayState(img_to_show, proc_fps, total_fps);

        imshow("Face Detection Demo", img_to_show);

        processKey(waitKey(3) & 0xff);
    }
}

void App::displayState(Mat& frame, double proc_fps, double total_fps)
{
    const Scalar fontColorRed = CV_RGB(255, 0, 0);

    int i = 0;

    ostringstream txt;
    txt.str(""); txt << "Source size: " << frame.cols << 'x' << frame.rows;
    printText(frame, txt.str(), i++);

    printText(frame, useGPU ? "Mode: CUDA" : "Mode: CPU", i++);

    txt.str(""); txt << "FPS (FD only): " << fixed << setprecision(1) << proc_fps;
    printText(frame, txt.str(), i++);

    txt.str(""); txt << "FPS (total): " << fixed << setprecision(1) << total_fps;
    printText(frame, txt.str(), i++);

    if (!showHelp)
    {
        printText(frame, "H - toggle hotkeys help", i++, fontColorRed);
    }
    else
    {
        printText(frame, "Space - switch GPU / CPU", i++, fontColorRed);
        printText(frame, "1/Q - increase/decrease scale", i++, fontColorRed);
        printText(frame, "M - switch OneFace / MultiFace", i++, fontColorRed);
        printText(frame, "F - toggle rectangles filter", i++, fontColorRed);
        if (sources.size() > 1)
            printText(frame, "N - next source", i++, fontColorRed);
    }
}

bool App::processKey(int key)
{
    if (BaseApp::processKey(key))
        return true;

    switch (toupper(key & 0xff))
    {
    case 32 /*space*/:
        useGPU = !useGPU;
        cout << "Switched to " << (useGPU ? "CUDA" : "CPU") << " mode\n";
        break;

    case 'H':
        showHelp = !showHelp;
        break;

    case '1':
        scaleFactor *= 1.05;
        cout << "Scale: " << scaleFactor << endl;
        break;

    case 'Q':
        scaleFactor /= 1.05;
        cout << "Scale: " << scaleFactor << endl;
        break;

    case 'M':
        findLargestObject = !findLargestObject;
        if (findLargestObject)
            cout << "OneFace mode" << endl;
        else
            cout << "MultiFace mode" << endl;
        break;

    case 'F':
        filterRects = !filterRects;
        if (filterRects)
            cout << "Enable rectangles filter" << endl;
        else
            cout << "Disable rectangles filter" << endl;
        break;

    case 'N':
        curSource = (curSource + 1) % sources.size();
        sources[curSource]->reset();
        cout << "Switch source to " << curSource << endl;
        break;

    default:
        return false;
    }

    return true;
}

bool App::parseCmdArgs(int& i, int argc, const char* argv[])
{
    string arg(argv[i]);

    if (arg == "--cascade")
    {
        ++i;

        if (i >= argc)
            THROW_EXCEPTION("Missing file name after " << arg);

        cascade_name = argv[i];

        return true;
    }

    return false;
}

void App::printHelp()
{
    cout << "This sample demonstrates Face Detection algorithm" << endl;
    cout << "Usage: demo_face_detection [--cascade <cascade_file>] [options]" << endl;
    cout << "Options:" << endl;
    BaseApp::printHelp();
}

RUN_APP(App)
