#include "features_matcher.h"

#include <iostream>
#include <map>

#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/utils/filesystem.hpp>

namespace
{
  template <typename T>
  void FscanfOrDie(FILE *fptr, const char *format, T *value)
  {
    int num_scanned = fscanf(fptr, format, value);
    if (num_scanned != 1)
    {
      std::cerr << "Invalid UW data file.";
      exit(-1);
    }
  }
}

FeatureMatcher::FeatureMatcher(cv::Mat intrinsics_matrix, cv::Mat dist_coeffs, double focal_scale)
{
  intrinsics_matrix_ = intrinsics_matrix.clone();
  dist_coeffs_ = dist_coeffs.clone();
  new_intrinsics_matrix_ = intrinsics_matrix.clone();
  new_intrinsics_matrix_.at<double>(0, 0) *= focal_scale;
  new_intrinsics_matrix_.at<double>(1, 1) *= focal_scale;
}

cv::Mat FeatureMatcher::readUndistortedImage(const std::string &filename)
{
  cv::Mat img = cv::imread(filename), und_img, dbg_img;
  cv::undistort(img, und_img, intrinsics_matrix_, dist_coeffs_, new_intrinsics_matrix_);

  return und_img;
}

void FeatureMatcher::extractFeatures()
{
  features_.resize(images_names_.size());
  descriptors_.resize(images_names_.size());
  feats_colors_.resize(images_names_.size());

  for (int i = 0; i < images_names_.size(); i++)
  {
    std::cout << "Computing descriptors for image " << i << std::endl;
    cv::Mat img = readUndistortedImage(images_names_[i]);

    //////////////////////////// Code to be completed (1/1) /////////////////////////////////
    // Extract salient points + descriptors from i-th image, and store them into
    // features_[i] and descriptors_[i] vector, respectively
    // Extract also the color (i.e., the cv::Vec3b information) of each feature, and store
    // it into feats_colors_[i] vector
    /////////////////////////////////////////////////////////////////////////////////////////ù

    // try to increase levels of pyramid and set scale near to 1
    cv::Ptr<cv::ORB> orb_detector = cv::ORB::create(10000);
    // orb_detector->detectAndCompute(img, cv::Mat(), features_[i], descriptors_[i]);
    orb_detector->detect(img, features_[i]);

    //-----------------------------------------------------
    // descriptor = cv::xfeatures2d::BEBLID_create(0.75)

    cv::Ptr<cv::DescriptorExtractor> orb_extractor = cv::ORB::create();
    orb_extractor->compute(img, features_[i], descriptors_[i]);

    //----------------------------

    cv::Vec3b tmp_color;
    for (int j = 0; j < features_[i].size(); j++)
    {
      tmp_color = img.at<cv::Vec3b>(cv::Point((int)features_[i][j].pt.x, (int)features_[i][j].pt.y));
      feats_colors_[i].push_back(tmp_color);
    }
  }
}

void FeatureMatcher::exhaustiveMatching()
{
  for (int i = 0; i < images_names_.size() - 1; i++)
  {
    for (int j = i + 1; j < images_names_.size(); j++)
    {
      std::cout << "Matching image " << i << " with image " << j << std::endl;
      std::vector<cv::DMatch> matches, inlier_matches;

      //////////////////////////// Code to be completed (2/5) /////////////////////////////////
      // Match descriptors between image i and image j, and perform geometric validation,
      // possibly discarding the outliers (remember that features have been extracted
      // from undistorted images that now has  new_intrinsics_matrix_ as K matrix and
      // no distortions)
      // As geometric models, use both the Essential matrix and the Homograph matrix,
      // both by setting new_intrinsics_matrix_ as K matrix
      // Do not set matches between two images if the amount of inliers matches
      // (i.e., geomatrically verified matches) is small (say <= 10 matches)
      /////////////////////////////////////////////////////////////////////////////////////////

      cv::Ptr<cv::BFMatcher> bf_matcher = cv::BFMatcher::create(cv::NORM_HAMMING, true);

      bf_matcher->match(descriptors_[i], descriptors_[j], matches);
      // std::vector<std::vector<cv::DMatch>> k_matches;
      // bf_matcher->knnMatch(descriptors_[i], descriptors_[j], k_matches, 2);
      //  Perform ratio test to improve quality of matches
      /*std::vector<cv::DMatch> good_matches;
      const float ratio = 0.6; // 0.8 in Lowe's paper
      for (int a = 0; a < k_matches.size(); a++)
      {
        if (k_matches[a][0].distance < ratio * k_matches[a][1].distance)
        {
          good_matches.push_back(k_matches[a][0]);
        }
      }*/

      std::vector<cv::Point2f> src_pts;
      std::vector<cv::Point2f> dst_pts;
      std::vector<int> inliers_h;
      cv::Mat inliers_e;
      for (int s = 0; s < matches.size(); s++)
      {
        src_pts.push_back(features_[i][matches[s].queryIdx].pt);
        dst_pts.push_back(features_[j][matches[s].trainIdx].pt);
      }

      cv::findHomography(src_pts, dst_pts, cv::RANSAC, 3, inliers_h);
      cv::findEssentialMat(src_pts, dst_pts, new_intrinsics_matrix_, cv::RANSAC, 0.999, 1.0, inliers_e);

      std::vector<int> inliers_E = (std::vector<int>)inliers_e;

      std::vector<int> best_model;
      int count_h = cv::countNonZero(inliers_h);
      std::cout << "inliers h = " << count_h << "\n";
      int count_e = cv::countNonZero(inliers_E);
      std::cout << "inliers e = " << count_e << "\n";

      if (count_h > count_e && count_h > 20)
      {
        best_model = inliers_h;
      }
      else if (count_e > count_h && count_e > 20)
      {
        best_model = inliers_E;
      }

      for (int h = 0; h < best_model.size(); h++)
      {
        if (best_model[h] == 1)
        {
          inlier_matches.push_back(matches[h]);
        }
      }

      if (inlier_matches.size() != 0)
      {
        setMatches(i, j, inlier_matches);
      }
    }
  }
}

void FeatureMatcher::writeToFile(const std::string &filename, bool normalize_points) const
{
  FILE *fptr = fopen(filename.c_str(), "w");

  if (fptr == NULL)
  {
    std::cerr << "Error: unable to open file " << filename;
    return;
  };

  fprintf(fptr, "%d %d %d\n", num_poses_, num_points_, num_observations_);

  double *tmp_observations;
  cv::Mat dst_pts;
  if (normalize_points)
  {
    cv::Mat src_obs(num_observations_, 1, cv::traits::Type<cv::Vec2d>::value, const_cast<double *>(observations_.data()));
    cv::undistortPoints(src_obs, dst_pts, new_intrinsics_matrix_, cv::Mat());
    tmp_observations = reinterpret_cast<double *>(dst_pts.data);
  }
  else
  {
    tmp_observations = const_cast<double *>(observations_.data());
  }

  for (int i = 0; i < num_observations_; ++i)
  {
    fprintf(fptr, "%d %d", pose_index_[i], point_index_[i]);
    for (int j = 0; j < 2; ++j)
    {
      fprintf(fptr, " %g", tmp_observations[2 * i + j]);
    }
    fprintf(fptr, "\n");
  }

  if (colors_.size() == 3 * num_points_)
  {
    for (int i = 0; i < num_points_; ++i)
      fprintf(fptr, "%d %d %d\n", colors_[i * 3], colors_[i * 3 + 1], colors_[i * 3 + 2]);
  }

  fclose(fptr);
}

void FeatureMatcher::testMatches(double scale)
{
  // For each pose, prepare a map that reports the pairs [point index, observation index]
  std::vector<std::map<int, int>> cam_observation(num_poses_);
  for (int i_obs = 0; i_obs < num_observations_; i_obs++)
  {
    int i_cam = pose_index_[i_obs], i_pt = point_index_[i_obs];
    cam_observation[i_cam][i_pt] = i_obs;
  }

  for (int r = 0; r < num_poses_; r++)
  {
    for (int c = r + 1; c < num_poses_; c++)
    {
      int num_mathces = 0;
      std::vector<cv::DMatch> matches;
      std::vector<cv::KeyPoint> features0, features1;
      for (auto const &co_iter : cam_observation[r])
      {
        if (cam_observation[c].find(co_iter.first) != cam_observation[c].end())
        {
          features0.emplace_back(observations_[2 * co_iter.second], observations_[2 * co_iter.second + 1], 0.0);
          features1.emplace_back(observations_[2 * cam_observation[c][co_iter.first]], observations_[2 * cam_observation[c][co_iter.first] + 1], 0.0);
          matches.emplace_back(num_mathces, num_mathces, 0);
          num_mathces++;
        }
      }
      cv::Mat img0 = readUndistortedImage(images_names_[r]),
              img1 = readUndistortedImage(images_names_[c]),
              dbg_img;

      cv::drawMatches(img0, features0, img1, features1, matches, dbg_img);
      cv::resize(dbg_img, dbg_img, cv::Size(), scale, scale);
      cv::imshow("", dbg_img);
      if (cv::waitKey() == 27)
        return;
    }
  }
}

void FeatureMatcher::setMatches(int pos0_id, int pos1_id, const std::vector<cv::DMatch> &matches)
{

  const auto &features0 = features_[pos0_id];
  const auto &features1 = features_[pos1_id];

  auto pos_iter0 = pose_id_map_.find(pos0_id),
       pos_iter1 = pose_id_map_.find(pos1_id);

  // Already included position?
  if (pos_iter0 == pose_id_map_.end())
  {
    pose_id_map_[pos0_id] = num_poses_;
    pos0_id = num_poses_++;
  }
  else
    pos0_id = pose_id_map_[pos0_id];

  // Already included position?
  if (pos_iter1 == pose_id_map_.end())
  {
    pose_id_map_[pos1_id] = num_poses_;
    pos1_id = num_poses_++;
  }
  else
    pos1_id = pose_id_map_[pos1_id];

  for (auto &match : matches)
  {

    // Already included observations?
    uint64_t obs_id0 = poseFeatPairID(pos0_id, match.queryIdx),
             obs_id1 = poseFeatPairID(pos1_id, match.trainIdx);
    auto pt_iter0 = point_id_map_.find(obs_id0),
         pt_iter1 = point_id_map_.find(obs_id1);
    // New point
    if (pt_iter0 == point_id_map_.end() && pt_iter1 == point_id_map_.end())
    {
      int pt_idx = num_points_++;
      point_id_map_[obs_id0] = point_id_map_[obs_id1] = pt_idx;

      point_index_.push_back(pt_idx);
      point_index_.push_back(pt_idx);
      pose_index_.push_back(pos0_id);
      pose_index_.push_back(pos1_id);
      observations_.push_back(features0[match.queryIdx].pt.x);
      observations_.push_back(features0[match.queryIdx].pt.y);
      observations_.push_back(features1[match.trainIdx].pt.x);
      observations_.push_back(features1[match.trainIdx].pt.y);

      // Average color between two corresponding features (suboptimal since we shouls also consider
      // the other observations of the same point in the other images)
      cv::Vec3f color = (cv::Vec3f(feats_colors_[pos0_id][match.queryIdx]) +
                         cv::Vec3f(feats_colors_[pos1_id][match.trainIdx])) /
                        2;

      colors_.push_back(cvRound(color[2]));
      colors_.push_back(cvRound(color[1]));
      colors_.push_back(cvRound(color[0]));

      num_observations_++;
      num_observations_++;
    }
    // New observation
    else if (pt_iter0 == point_id_map_.end())
    {
      int pt_idx = point_id_map_[obs_id1];
      point_id_map_[obs_id0] = pt_idx;

      point_index_.push_back(pt_idx);
      pose_index_.push_back(pos0_id);
      observations_.push_back(features0[match.queryIdx].pt.x);
      observations_.push_back(features0[match.queryIdx].pt.y);
      num_observations_++;
    }
    else if (pt_iter1 == point_id_map_.end())
    {
      int pt_idx = point_id_map_[obs_id0];
      point_id_map_[obs_id1] = pt_idx;

      point_index_.push_back(pt_idx);
      pose_index_.push_back(pos1_id);
      observations_.push_back(features1[match.trainIdx].pt.x);
      observations_.push_back(features1[match.trainIdx].pt.y);
      num_observations_++;
    }
    //    else if( pt_iter0->second != pt_iter1->second )
    //    {
    //      std::cerr<<"Shared observations does not share 3D point!"<<std::endl;
    //    }
  }
}
void FeatureMatcher::reset()
{
  point_index_.clear();
  pose_index_.clear();
  observations_.clear();
  colors_.clear();

  num_poses_ = num_points_ = num_observations_ = 0;
}