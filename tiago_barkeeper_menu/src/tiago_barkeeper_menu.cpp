#ifndef NDEBUG
#define NDEBUG
#endif

#include <boost/filesystem.hpp>

#include <ros/ros.h>

#include <eigen3/Eigen/StdVector>

#include <cv_bridge/cv_bridge.h>
#include <image_transport/image_transport.h>
#include <opencv2/highgui/highgui.hpp>

#include <thread>

#include <array>
#include <assert.h>
#include <chrono>
#include <iostream>
#include <mutex>
#include <random>
#include <unordered_set>
#include <vector>

#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Dense>

#include <boost/functional/hash.hpp>

#include <opencv2/opencv.hpp>

#include <opencv2/bgsegm.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/optflow.hpp>
#include <opencv2/xfeatures2d.hpp>

#include <yaml-cpp/yaml.h>

#include <std_msgs/String.h>

int main(int argc, char **argv) {

  ros::init(argc, argv, "test", ros::init_options::NoSigintHandler);

  ros::NodeHandle node;

  ros::AsyncSpinner spinner(4);
  spinner.start();

  cv::startWindowThread();

  cv::FileStorage cfg(argv[1], cv::FileStorage::READ);

  YAML::Node yaml_calibration = YAML::LoadFile(argv[2]);

  cv::RNG rng;

  std::cout << cfg["template"].string() << std::endl;

  auto image_path = boost::filesystem::path(argv[1]).parent_path() /
                    boost::filesystem::path(cfg["template"].string());
  std::cout << image_path << std::endl;
  auto image_object = cv::imread(image_path.string());

  ros::Publisher selection_pub =
      node.advertise<std_msgs::String>("selection", 1);

  static cv::Mat image_denoise_sum;
  static float image_denoise_div = 0;
  static std::mutex image_denoise_mutex;

  static cv_bridge::CvImagePtr image_ptr;
  image_transport::ImageTransport it(node);
  image_transport::Subscriber sub =
      it.subscribe("/camera/rgb/image_raw", 1,
                   (void (*)(const sensor_msgs::ImageConstPtr &))[](
                       const sensor_msgs::ImageConstPtr &msg) {
                     if (1) {
                       auto img = cv_bridge::toCvCopy(msg, "bgr8");
                       image_ptr = img;
                       // cv::imshow("received", img->image);
                     }

                     if (1) {
                       auto imgx = cv_bridge::toCvCopy(msg, "bgr8");
                       auto img = imgx->image;
                       cv::Mat image_scene_float;
                       img.convertTo(image_scene_float, CV_32FC3);
                       {
                         std::unique_lock<std::mutex> lock(image_denoise_mutex);
                         if (image_denoise_sum.rows == 0) {
                           // std::cout << "x" << std::endl;
                           image_denoise_sum = image_scene_float;
                         } else {
                           // std::cout << "y" << std::endl;
                           image_denoise_sum += image_scene_float;
                         }
                         image_denoise_div++;
                       }
                     }
                   });

  // cv::resize(image_object, image_object, cv::Size(), 0.5, 0.5);

  auto area_path = boost::filesystem::path(argv[1]).parent_path() /
                   boost::filesystem::path(cfg["areas"].string());
  std::cout << area_path << std::endl;
  auto image_areas = cv::imread(area_path.string());

  // image_areas.convertTo(image_areas, CV_8UC4);

  /*auto image_scene = cv::imread(argv[2]);
  cv::cvtColor(image_scene.clone(), image_scene, CV_BGR2GRAY);
  cv::resize(image_scene, image_scene, cv::Size(), 0.5, 0.5);*/

  cv::resize(image_object, image_object, cv::Size(), 0.125, 0.125);
  cv::resize(image_areas, image_areas, cv::Size(), 0.125, 0.125);

  auto image_object_color = image_object;
  cv::cvtColor(image_object, image_object, CV_BGR2GRAY);

  cv::imshow("areas", image_areas);

  cv::GaussianBlur(image_object, image_object, cv::Size(15, 15), 0, 0);

  auto image_object0 = image_object;

  /*cv::VideoCapture cap;
  if (!cap.open(0)) {
    std::cout << "failed to open camera" << std::endl;
    return -1;
  }*/

  cv::Ptr<cv::xfeatures2d::SIFT> detector =
      cv::xfeatures2d::SIFT::create(10000);

  std::vector<cv::KeyPoint> keypoints_object;
  cv::Mat descriptors_object;

  detector->detectAndCompute(image_object, cv::noArray(), keypoints_object,
                             descriptors_object);

  cv::imshow("a", image_object);

  cv::namedWindow("finger", cv::WINDOW_NORMAL);
  cv::resizeWindow("finger", 1000, 1000);

  cv::Size table_image_size = cv::Size(1024, 1024);
  cv::Mat table_transform;
  {
    auto size = table_image_size;
    float scale = 0.1f;
    cv::Point2f src[4] = {
        cv::Point2f(yaml_calibration[0].as<float>(),
                    yaml_calibration[1].as<float>()),
        cv::Point2f(yaml_calibration[2].as<float>(),
                    yaml_calibration[3].as<float>()),
        cv::Point2f(yaml_calibration[6].as<float>(),
                    yaml_calibration[7].as<float>()),
        cv::Point2f(yaml_calibration[4].as<float>(),
                    yaml_calibration[5].as<float>()),
    };
    cv::Point2f dst[4] = {
        cv::Point2f(size.width * 0.5f - size.width * scale,
                    size.height * 0.5f - size.height * scale),
        cv::Point2f(size.width * 0.5f + size.width * scale,
                    size.height * 0.5f - size.height * scale),
        cv::Point2f(size.width * 0.5f + size.width * scale,
                    size.height * 0.5f + size.height * scale),
        cv::Point2f(size.width * 0.5f - size.width * scale,
                    size.height * 0.5f + size.height * scale),
    };
    for (size_t i = 0; i < 4; i++)
      std::cout << src[i].x << " " << src[i].y << " " << dst[i].x << " "
                << dst[i].y << std::endl;
    table_transform = cv::getPerspectiveTransform(src, dst);
  }

  cv::FlannBasedMatcher matcher;

  std::mutex transform_x_mutex;
  cv::Mat transform_x_mat = cv::Mat();

  cv::waitKey(1);

  std::thread([&]() {
    while (true) {
      // cv::waitKey(1);

      usleep(1000000);

      std::vector<cv::KeyPoint> keypoints_scene;
      cv::Mat descriptors_scene;

      cv::Mat image_scene_color;
      auto img_ptr = image_ptr;
      if (!img_ptr)
        continue;

      // cv::Mat captured_image = img_ptr->image;
      cv::Mat captured_image;
      {
        std::unique_lock<std::mutex> lock(image_denoise_mutex);
        if (image_denoise_div < 8)
          continue;
        captured_image = image_denoise_sum * (1.0f / image_denoise_div);
        image_denoise_div = 0;
        image_denoise_sum.setTo(cv::Scalar(0));
        captured_image.convertTo(captured_image, CV_8UC3);
      }

      cv::warpPerspective(captured_image, image_scene_color, table_transform,
                          table_image_size);

      image_object = image_object0.clone();

      cv::Mat image_scene;
      cv::cvtColor(image_scene_color, image_scene, CV_BGR2GRAY);

      std::cout << "aa" << std::endl;

      /*cv::Mat image_scene_float;
      image_scene.convertTo(image_scene_float, CV_32FC1);
      static cv::Mat image_scene_float_denoised = image_scene_float;
      image_scene_float_denoised =
          image_scene_float_denoised * 0.9f + image_scene_float * 0.1f;
      image_scene_float_denoised.convertTo(image_scene, CV_8UC1);*/

      detector->detectAndCompute(image_scene, cv::noArray(), keypoints_scene,
                                 descriptors_scene);
      std::cout << keypoints_object.size() << " " << keypoints_scene.size()
                << std::endl;
      if (keypoints_object.size() <= 1 || keypoints_scene.size() <= 1)
        continue;

      std::vector<std::vector<cv::DMatch>> knn_matches;
      matcher.knnMatch(descriptors_object, descriptors_scene, knn_matches, 2);

      std::vector<cv::DMatch> matches;
      for (size_t i = 0; i < knn_matches.size(); i++) {
        if (knn_matches[i].size() > 1 &&
            knn_matches[i][0].distance / knn_matches[i][1].distance <= 0.7f) {
          matches.push_back(knn_matches[i][0]);
        }
      }

      std::cout << "matches " << matches.size() << std::endl;
      if (matches.size() < 3)
        continue;

      {
        cv::Mat img_matches;
        cv::drawMatches(image_object, keypoints_object, image_scene,
                        keypoints_scene, matches, img_matches);
        // cv::imshow("matches", img_matches);
      }

      std::vector<cv::Point2f> points_object;
      std::vector<cv::Point2f> points_scene;
      for (auto &m : matches) {
        points_object.push_back(keypoints_object[m.queryIdx].pt);
        points_scene.push_back(keypoints_scene[m.trainIdx].pt);
      }

      cv::Mat transform =
          cv::estimateRigidTransform(points_scene, points_object, false);

      try {
        transform.convertTo(transform, CV_32FC1);
        cv::findTransformECC(
            image_scene, image_object, transform, cv::MOTION_AFFINE,
            cv::TermCriteria(cv::TermCriteria::COUNT, 5, 0.001));
      } catch (const cv::Exception &e) {
        std::cout << e.what() << std::endl;
        continue;
      }

      if (transform.empty()) {
        std::cout << "no transform" << std::endl;
        continue;
      }

      {
        std::unique_lock<std::mutex> lock(transform_x_mutex);
        transform_x_mat = transform;
      }

      std::cout << transform << std::endl;
    }
  })
      .detach();

  // ros::spin();

  while (true) {

    cv::waitKey(1);

    cv::Mat transform;

    {
      std::unique_lock<std::mutex> lock(transform_x_mutex);
      transform = transform_x_mat;
    }

    ros::Time current_time = ros::Time::now();
    static ros::Time last_time = current_time;
    double delta_time = (current_time - last_time).toSec();
    last_time = current_time;

    cv::Mat image_scene_color;
    auto img_ptr = image_ptr;
    if (!img_ptr)
      continue;

    cv::Mat captured_image = img_ptr->image;
    cv::warpPerspective(captured_image, image_scene_color, table_transform,
                        table_image_size);

    cv::imshow("input", image_scene_color);

    image_object = image_object0.clone();

    cv::Mat image_scene;
    cv::cvtColor(image_scene_color, image_scene, CV_BGR2GRAY);

    /*{
      std::unique_lock<std::mutex> lock(image_denoise_mutex);
      image_scene = image_denoise_sum * (1.0f / image_denoise_div);
      image_scene.convertTo(image_scene, CV_8UC1);
    }*/

    cv::imshow("gray", image_scene);

    if (transform.empty()) {
      continue;
    }

    cv::Mat image_object_unprojected;
    cv::warpAffine(image_scene_color, image_object_unprojected, transform,
                   image_object.size());

    cv::imshow("image_object_unprojected", image_object_unprojected);

    static cv::Mat mask;

    //

    /*static cv::Ptr<cv::BackgroundSubtractor> background_subtractor =
        cv::createBackgroundSubtractorMOG2();

    background_subtractor->apply(captured_image, mask);
    cv::imshow("mask0", mask);
    cv::warpPerspective(mask, mask, table_transform, table_image_size);
    cv::warpAffine(mask, mask, transform, image_object.size());

    cv::imshow("maskraw", mask);

    cv::threshold(mask, mask, 240, 255, 0);

    cv::GaussianBlur(mask, mask, cv::Size(21, 21), 0, 0);

    cv::threshold(mask, mask, 50, 255, 0);

    cv::GaussianBlur(mask, mask, cv::Size(21, 21), 0, 0);

    cv::threshold(mask, mask, 50, 255, 0);*/

    //

    // mask = image_object_unprojected.dot(cv::Vec3f(1, 0, -1));
    cv::Mat channels[4];
    cv::split(image_object_unprojected, channels);
    mask = channels[2] - channels[0];

    cv::imshow("mask0", mask);

    cv::threshold(mask, mask, 10, 255, 0);

    std::vector<std::vector<cv::Point>> polygons;
    cv::findContours(mask, polygons, CV_RETR_LIST, CV_CHAIN_APPROX_SIMPLE);
    std::vector<cv::Point> polygon;
    if (!polygons.empty()) {
      polygon = polygons.front();
      for (auto &p : polygons)
        if (cv::contourArea(p) > cv::contourArea(polygon))
          polygon = p;
    }
    polygons.clear();
    polygons.push_back(polygon);
    mask.setTo(cv::Scalar(0));
    if (!polygons.empty() && !polygons[0].empty())
      cv::drawContours(mask, polygons, -1, cv::Scalar(255), CV_FILLED);

    //

    cv::imshow("mask", mask);

    cv::Mat border = mask.clone();
    cv::rectangle(border, cv::Point(1, 1),
                  cv::Point(border.cols - 2, border.rows - 2), cv::Scalar(0),
                  CV_FILLED);
    border.convertTo(border, CV_32FC1);
    cv::GaussianBlur(border, border, cv::Size(1001, 1001), 0, 0);

    mask.convertTo(mask, CV_32FC1);
    border += 255 - mask;

    cv::imshow("border", border);

    cv::Point fingerTip(0, 0);
    cv::minMaxLoc(border, 0, 0, &fingerTip, 0);

    cv::circle(image_object_unprojected, fingerTip, 5, cv::Scalar(0, 0, 255));
    cv::circle(image_object_unprojected, fingerTip, 20, cv::Scalar(0, 0, 255));

    static std::string current_selection;
    static double current_level = 0;

    if (fingerTip == cv::Point(0, 0)) {
      cv::imshow("finger", image_object_unprojected);
      std::cout << "no finger" << std::endl;
      current_selection = "";
      current_level = 0;
      continue;
    }

    cv::Vec3b colorv = image_areas.at<cv::Vec3b>(fingerTip.y, fingerTip.x);

    uint32_t color_key = ((uint32_t)colorv[2] << 16) |
                         ((uint32_t)colorv[1] << 8) |
                         ((uint32_t)colorv[0] << 0);

    std::stringstream key_stream;
    key_stream << std::hex << std::uppercase << std::setfill('0')
               << std::setw(6) << color_key;
    std::string key_str = key_stream.str();

    std::cout << "key " << key_str << std::endl;

    std::string selection = cfg["labels"][key_str];

    std::cout << "selection " << selection << std::endl;

    if (!selection.empty()) {
      cv::putText(image_object_unprojected, key_str, cv::Point(100, 100),
                  CV_FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 0, 0), 2,
                  cv::LINE_AA);

      cv::putText(image_object_unprojected, selection, cv::Point(100, 200),
                  CV_FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(255, 0, 0), 2,
                  cv::LINE_AA);
    }

    if (selection != current_selection) {
      current_selection = selection;
      current_level = 0;
    }
    static double last_level = current_level;
    current_level = std::min(1.0, current_level + delta_time / 1.8);

    if (!selection.empty()) {
      cv::putText(image_object_unprojected, std::to_string(current_level),
                  cv::Point(100, 300), CV_FONT_HERSHEY_SIMPLEX, 0.8,
                  cv::Scalar(255, 0, 0), 2, cv::LINE_AA);
    }

    if (last_level < 1 && current_level >= 1 && !selection.empty()) {
      cv::rectangle(image_object_unprojected, cv::Point(0, 0),
                    cv::Point(image_object_unprojected.cols,
                              image_object_unprojected.rows),
                    cv::Scalar(0, 1, 0), CV_FILLED);

      std_msgs::String msg;
      msg.data = selection;
      selection_pub.publish(msg);
    }

    last_level = current_level;

    std::cout << current_level << " " << current_selection << std::endl;

    cv::imshow("finger", image_object_unprojected);
  }
}
