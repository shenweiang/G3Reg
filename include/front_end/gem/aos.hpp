#ifndef TRAVEL_OSEG_H
#define TRAVEL_OSEG_H
//
// Created by Minho Oh & Euigon Jung on 1/31/22.
// We really appreciate Hyungtae Lim and Prof. Hyun Myung! :)
//
#pragma once

#include <vector>
#include <algorithm>
#include <iostream>
#include <list>
#include <numeric>
#include <random>
#include <chrono>
#include <forward_list>
#include <boost/optional.hpp>

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/common/io.h>
#include <fstream>
#include <yaml-cpp/yaml.h>

using namespace std;

namespace travel {

    class AOSNode {
    public:
        uint start;
        uint end;
        int label;

        AOSNode() : label(-1) {}
    };

    class Point {
    public:
        float x, y, z;
        uint idx;
        bool valid = false;
        int label;
    };

    template<typename T>
    class ObjectCluster {
    private:
        uint16_t max_label_;
        vector<forward_list<Point *>> clusters_;
        forward_list<Point *> empty_;
        vector<int> valid_cnt_;
        vector<vector<AOSNode>> nodes_;
        vector<float> vert_angles_;
        vector<vector<Point>> range_mat_;
        vector<vector<Point>> emptyrange_mat_;

        // parameters
        std::string dataset_name_;
        int VERT_SCAN;
        int HORZ_SCAN;
        float MIN_RANGE;
        float MAX_RANGE;
        float HORZ_MERGE_THRES;
        float VERT_MERGE_THRES;
        int VERT_SCAN_SIZE;
        int HORZ_SCAN_SIZE;
        int HORZ_SKIP_SIZE;
        int HORZ_EXTENSION_SIZE;
        int DOWNSAMPLE;
        float MIN_VERT_ANGLE;
        float MAX_VERT_ANGLE;
        int MIN_CLUSTER_SIZE;
        int MAX_CLUSTER_SIZE;

        // test
        // vector<vector<vector<std::pair<float, float>>>> point_angle_;

    private:
        template<typename TScalar>
        TScalar get(const YAML::Node &node, const std::string &father_key, const std::string &key,
                    const TScalar &default_value) {
            if (!node[father_key] || !node[father_key][key]) {
//                std::cout << "Key " << father_key << "/" << key << " not found, using default value: " << default_value << std::endl;
                return default_value;
            }
            TScalar value = node[father_key][key].as<TScalar>();
//            std::cout << "Key " << father_key << "/" << key << " found, using value: " << value << std::endl;
            return value;
        }

    public:
        ObjectCluster(std::string config_file) {
//                setParams();

            YAML::Node node = YAML::LoadFile(config_file);
            VERT_SCAN = get(node, "travel", "vert_scan", VERT_SCAN);
            HORZ_SCAN = get(node, "travel", "horz_scan", HORZ_SCAN);
            MIN_VERT_ANGLE = get(node, "travel", "min_vert_angle", MIN_VERT_ANGLE);
            MAX_VERT_ANGLE = get(node, "travel", "max_vert_angle", MAX_VERT_ANGLE);
            DOWNSAMPLE = get(node, "travel", "downsample", DOWNSAMPLE);
            HORZ_MERGE_THRES = get(node, "travel", "horz_merge_thres", HORZ_MERGE_THRES);
            VERT_MERGE_THRES = get(node, "travel", "vert_merge_thres", VERT_MERGE_THRES);
            VERT_SCAN_SIZE = get(node, "travel", "vert_scan_size", VERT_SCAN_SIZE);
            HORZ_SCAN_SIZE = get(node, "travel", "horz_scan_size", HORZ_SCAN_SIZE);
            HORZ_SKIP_SIZE = get(node, "travel", "horz_skip_size", HORZ_SKIP_SIZE);
            HORZ_EXTENSION_SIZE = get(node, "travel", "horz_extension_size", HORZ_EXTENSION_SIZE);
            MIN_RANGE = get(node, "travel", "min_range", MIN_RANGE);
            MAX_RANGE = get(node, "travel", "max_range", MAX_RANGE);
            MIN_CLUSTER_SIZE = get(node, "travel", "min_cluster_size", MIN_CLUSTER_SIZE);
            MAX_CLUSTER_SIZE = get(node, "travel", "max_cluster_size", MAX_CLUSTER_SIZE);

            float resolution = (float) (MAX_VERT_ANGLE - MIN_VERT_ANGLE) / (float) (VERT_SCAN - 1);

            for (int i = 0; i < VERT_SCAN; i++)
                vert_angles_.push_back(MIN_VERT_ANGLE + i * resolution);

            range_mat_.resize(VERT_SCAN, vector<Point>(HORZ_SCAN));

            valid_cnt_.resize(VERT_SCAN, 0);
            nodes_.resize(VERT_SCAN, vector<AOSNode>());

            empty_ = {};
        }

        ~ObjectCluster() {}

        void setParams(double min_range, double max_range, int min_cluster_size) {
            MIN_RANGE = min_range;
            MAX_RANGE = max_range;
            MIN_CLUSTER_SIZE = min_cluster_size;
        }

        void setParams() {
            if (dataset_name_ == "KITTI") {
                MIN_RANGE = 1.0;
                MAX_RANGE = 64.0;
                VERT_SCAN = 64;
                HORZ_SCAN = 4500;
                MIN_VERT_ANGLE = -24.8;
                MAX_VERT_ANGLE = 2.0;

                DOWNSAMPLE = 1;
                HORZ_MERGE_THRES = 0.4;
                VERT_MERGE_THRES = 0.5;
                VERT_SCAN_SIZE = 3;
                HORZ_SCAN_SIZE = 5;
                HORZ_SKIP_SIZE = 5;
                HORZ_EXTENSION_SIZE = 5;
                MIN_CLUSTER_SIZE = 10;
                MAX_CLUSTER_SIZE = 30000;
            }

            float resolution = (float) (MAX_VERT_ANGLE - MIN_VERT_ANGLE) / (float) (VERT_SCAN - 1);

            for (int i = 0; i < VERT_SCAN; i++)
                vert_angles_.push_back(MIN_VERT_ANGLE + i * resolution);

            range_mat_.resize(VERT_SCAN, vector<Point>(HORZ_SCAN));

            valid_cnt_.resize(VERT_SCAN, 0);
            nodes_.resize(VERT_SCAN, vector<AOSNode>());
        }

        void segmentObjects(std::shared_ptr<pcl::PointCloud<T>> cloud_in,
                            std::vector<std::shared_ptr<pcl::PointCloud<T>>> &clusters,
                            vector<float> *vert_angles = nullptr) {
            // 0. reset
            max_label_ = 1;
            clusters_.clear();
            clusters_.push_back(empty_);
            clusters_.push_back(empty_);
            std::fill(valid_cnt_.begin(), valid_cnt_.end(), 0);
            std::for_each(range_mat_.begin(), range_mat_.end(), [](vector<Point> &inner_vec) {
                std::fill(inner_vec.begin(), inner_vec.end(), Point());
            });
            // 1. do spherical projection
            auto start = chrono::steady_clock::now();
            sphericalProjection(cloud_in);
            auto end = chrono::steady_clock::now();
            // cloud_out->points.resize(cloud_in->points.size());
            //printf("Creating range map: %ld ms\n", chrono::duration_cast<chrono::milliseconds>(end-start).count());

            // 2. begin clustering
            start = chrono::steady_clock::now();
            for (int channel = 0; channel < VERT_SCAN; channel++) {
                auto horz_start = chrono::steady_clock::now();
                horizontalUpdate(channel);
                auto horz_end = chrono::steady_clock::now();
                auto vert_start = chrono::steady_clock::now();
                verticalUpdate(channel);
                auto vert_end = chrono::steady_clock::now();
                // printf("Channel %d: pt count: %d, node count: %d, horz: %ld ms, vert: %ld ms\n", channel,
                //     valid_cnt_[channel], nodes_[channel].size(),
                //     chrono::duration_cast<chrono::milliseconds>(horz_end-horz_start).count(),
                //     chrono::duration_cast<chrono::milliseconds>(vert_end-vert_start).count());
            }
            end = chrono::steady_clock::now();
            //printf("Processing clustering: %ld ms\n", chrono::duration_cast<chrono::milliseconds>(end-start).count());

            // 3. post-processing (clusters -> labels)
            start = chrono::steady_clock::now();
            labelPointcloud(cloud_in, clusters);
            end = chrono::steady_clock::now();
            //printf("Post-processing: %ld ms\n", chrono::duration_cast<chrono::milliseconds>(end-start).count());
        }

        void sphericalProjection(std::shared_ptr<pcl::PointCloud<T>> cloud_in) {
            std::shared_ptr<pcl::PointCloud<T>> valid_cloud = std::make_shared<pcl::PointCloud<T>>();
            valid_cloud->points.reserve(cloud_in->points.size());
            int ring_idx = -1, row_idx = -1, col_idx = -1;
            float range;
            Point point;
            int range_chcker = 0;
            int valid_checker = 0;
            int count_range, count_downsample, count_out_of_row, count_out_of_col, count_out_of_mat;
            count_range = count_downsample = count_out_of_row = count_out_of_col = count_out_of_mat = 0;
            int count_pass = 0;
            for (size_t i = 0; i < cloud_in->points.size(); i++) {

                range = getRange(cloud_in->points[i]);
                if (range < MIN_RANGE || range > MAX_RANGE) {
                    count_range++;
                    continue;
                }
                row_idx = getRowIdx(cloud_in->points[i]);
                if (row_idx % DOWNSAMPLE != 0) {
                    count_downsample++;
                    continue;
                }
                if (row_idx < 0 || row_idx >= VERT_SCAN) {
                    count_out_of_row++;
                    continue;
                }
                col_idx = getColIdx(cloud_in->points[i]);
                if (col_idx < 0 || col_idx >= HORZ_SCAN) {
                    count_out_of_col++;
                    continue;
                }
                if (range_mat_[row_idx][col_idx].valid) {
                    count_out_of_mat++;
                    continue;
                } else {
                    count_pass++;
                    point.x = cloud_in->points[i].x;
                    point.y = cloud_in->points[i].y;
                    point.z = cloud_in->points[i].z;
                    point.valid = true;
                    point.idx = valid_cloud->points.size();
                    valid_cloud->points.push_back(cloud_in->points[i]);
                    range_mat_[row_idx][col_idx] = point;
                    assert(range_mat_[row_idx][col_idx].valid == true);
                    valid_cnt_[row_idx]++;
                }
            }
            //printf("Input cloud size: %d\n", (int)cloud_in->points.size());
            //printf("Projected cloud size: %d\n", (int)valid_cloud->points.size());
            *cloud_in = *valid_cloud;
        }

        void labelPointcloud(std::shared_ptr<pcl::PointCloud<T>> cloud_in,
                             std::vector<std::shared_ptr<pcl::PointCloud<T>>> &clusters) {
            uint cnt = 0;
            // generate random number
            std::vector<size_t> valid_indices;
            for (size_t i = 0; i < clusters_.size(); i++) {
                if (std::distance(clusters_[i].begin(), clusters_[i].end()) > 0) {
                    valid_indices.push_back(i);
                    cnt++;
                }
            }
            std::list<uint16_t> l(cnt);
            std::iota(l.begin(), l.end(), 1);
            std::vector<std::list<uint16_t>::iterator> v(l.size());
            std::iota(v.begin(), v.end(), l.begin());
            std::shuffle(v.begin(), v.end(), std::mt19937{std::random_device{}()});

            for (size_t i = 0; i < valid_indices.size(); i++) {
                uint16_t label = *v[i];
                size_t idx = valid_indices[i];
                int point_size = std::distance(clusters_[idx].begin(), clusters_[idx].end());
                if (MIN_CLUSTER_SIZE && MAX_CLUSTER_SIZE) {
                    if (point_size < MIN_CLUSTER_SIZE || point_size > MAX_CLUSTER_SIZE)
                        continue;
                }

                std::shared_ptr<pcl::PointCloud<T>> cluster(new pcl::PointCloud<T>);
                for (auto &p: clusters_[idx]) {
                    if (p->valid) {
                        assert(cloud_in->points[p->idx].x == p->x);
                        assert(cloud_in->points[p->idx].y == p->y);
                        assert(cloud_in->points[p->idx].z == p->z);
                        //T point = cloud_in->points[p->idx];
                        //point.intensity = label;
                        //cloud_out->points.push_back(point);
                        T point;
                        point.x = p->x;
                        point.y = p->y;
                        point.z = p->z;
//                            point.intensity = p->label;
                        cluster->points.push_back(point);
                    } else {
                        //ROS_ERROR("point invalid");
                    }
                }
                clusters.push_back(cluster);
            }
            // assert(cloud_in->points.size() == cloud_out->points.size());
            ////printf("# of cluster: %d, point: %d\n", cnt, pt_cnt);
        }

        void horizontalUpdate(int channel) {
            nodes_[channel].clear();
            // no point in the channel
            if (valid_cnt_[channel] <= 0) {
                return;
            }

            int first_valid_idx = -1;
            int last_valid_idx = -1;
            int start_pos = -1;
            int pre_pos = -1;
            int end_pos = -1;

            AOSNode node;
            bool first_node = false;
            for (int j = 0; j < HORZ_SCAN; j++) {
                if (!range_mat_[channel][j].valid)
                    continue;
                if (!first_node) {
                    first_node = true;
                    // update index
                    first_valid_idx = j;
                    start_pos = j;
                    pre_pos = j;
                    // update label
                    max_label_++;
                    clusters_.push_back(empty_);
                    range_mat_[channel][j].label = max_label_;
                    // push a new node
                    node.start = start_pos;
                    node.end = j;
                    node.label = range_mat_[channel][j].label;
                    nodes_[channel].push_back(node);
                    clusters_[node.label].insert_after(clusters_[node.label].cbefore_begin(), &range_mat_[channel][j]);
                } else {
                    auto &cur_pt = range_mat_[channel][j];
                    auto &pre_pt = range_mat_[channel][pre_pos];
                    if (pointDistance(cur_pt, pre_pt) < HORZ_MERGE_THRES) {
                        // update existing node
                        pre_pos = j;
                        cur_pt.label = pre_pt.label;
                        nodes_[channel].back().end = j;
                    } else {
                        // update index
                        start_pos = j;
                        pre_pos = j;
                        // update label
                        max_label_++;
                        clusters_.push_back(empty_);
                        cur_pt.label = max_label_;
                        // push new node
                        node.start = start_pos;
                        node.end = j;
                        node.label = range_mat_[channel][j].label;
                        nodes_[channel].push_back(node);
                        assert(range_mat_[channel][j].label == cur_pt.label);
                    }
                    clusters_[cur_pt.label].insert_after(clusters_[cur_pt.label].cbefore_begin(),
                                                         &range_mat_[channel][j]);
                }
            }
            last_valid_idx = pre_pos;

            // merge last and first points
            if (nodes_[channel].size() > 2) {
                auto &p_0 = range_mat_[channel][first_valid_idx];
                auto &p_l = range_mat_[channel][last_valid_idx];
                if (pointDistance(p_0, p_l) < HORZ_MERGE_THRES) {
                    if (p_0.label == 0)
                        //printf("Ring merge to label 0\n");
                        if (p_0.label != p_l.label) {
                            nodes_[channel].back().label = p_0.label;
                            mergeClusters(p_l.label, p_0.label);
                        }
                }
            }

            // merge skipped nodes due to occlusion
            if (nodes_[channel].size() > 2) {
                uint16_t cur_label = 0, target_label = 0;
                for (size_t i = 0; i < nodes_[channel].size() - 1; i++) {
                    for (size_t j = i + 1; j < nodes_[channel].size(); j++) {
                        auto &node_i = nodes_[channel][i];
                        auto &node_j = nodes_[channel][j];

                        if (node_i.label == node_j.label)
                            continue;

                        int end_idx = node_i.end;
                        int start_idx = node_j.start;
                        float dist = pointDistance(range_mat_[channel][end_idx], range_mat_[channel][start_idx]);
                        if (dist < HORZ_MERGE_THRES) {
                            if (node_i.label > node_j.label) {
                                target_label = node_j.label;
                                cur_label = node_i.label;
                                node_i.label = target_label;
                            } else {
                                target_label = node_i.label;
                                cur_label = node_j.label;
                                node_j.label = target_label;
                            }
                            mergeClusters(cur_label, target_label);
                            // if (DEBUG)
                            //     //printf("merge two labels despite occlusion: %d %d\n", cur_label, target_label);
                        }
                        if (j - i >= HORZ_SKIP_SIZE)
                            break;
                    }
                }
            }
        }

        void verticalUpdate(int channel) {
            // Iterate each point of this channel to update the labels.
            int point_size = valid_cnt_[channel];
            // Current scan line is emtpy, do nothing.
            if (point_size == 0) return;
            // Skip first scan line
            if (channel == 0) return;

            int prev_channel = channel - 1;

            for (int n = 0; n < nodes_[channel].size(); n++) {

                auto &cur_node = nodes_[channel][n];

                for (int l = prev_channel; l >= channel - VERT_SCAN_SIZE; l -= 1) {

                    if (l < 0) // out of range
                        break;

                    if (valid_cnt_[l] == 0) {
                        // if (DEBUG)
                        //     //printf("No valid points in the channel\n");
                        continue;
                    }

                    // binary search lower bound
                    // lower_bnd inclusive
                    int N = nodes_[l].size();
                    int first = 0;
                    int last = N - 1;
                    int lower_bnd = 0;
                    int mid = 0;

                    while (first <= last) {
                        mid = (first + last) / 2;
                        auto &prev_node = nodes_[l][mid];
                        if (overlap(cur_node, prev_node) || cur_node.end < prev_node.start) {
                            lower_bnd = mid;
                            last = mid - 1;
                        } else {
                            first = mid + 1;
                        }
                    }

                    // binary search upper bound
                    // exclusive but gives -1 if end of list
                    first = 0;
                    last = N - 1;
                    mid = 0;
                    int upper_bnd = 0;

                    while (first <= last) {
                        mid = (first + last) / 2;
                        auto &prev_node = nodes_[l][mid];
                        if (overlap(cur_node, prev_node) || prev_node.end < cur_node.start) {
                            upper_bnd = mid;
                            first = mid + 1;
                        } else {
                            last = mid - 1;
                        }
                    }
                    upper_bnd = upper_bnd + 1;

                    // loop through overlapped nodes
                    for (size_t idx = lower_bnd; idx < upper_bnd; idx++) {

                        auto &ovl_node = nodes_[l][idx];

                        if (ovl_node.label == cur_node.label) {
                            // if (DEBUG)
                            //     //printf("same label: %d, %d\n", ovl_node.label, cur_node.label);
                            continue;
                        }

                        // if (DEBUG)
                        //     //printf("overlapped: %d, %d\n", ovl_node.start, ovl_node.end);

                        int iter_start_idx = -1;
                        int iter_end_idx = -1;

                        if (ovl_node.start <= cur_node.start && cur_node.end <= ovl_node.end) {
                            // cur_node inside prev_node
                            iter_start_idx = cur_node.start;
                            iter_end_idx = cur_node.end;
                        } else if (cur_node.start <= ovl_node.start && ovl_node.end <= cur_node.end) {
                            // prev_node inside cur_node
                            iter_start_idx = ovl_node.start;
                            iter_end_idx = ovl_node.end;
                        } else if (cur_node.start < ovl_node.start && cur_node.end >= ovl_node.start &&
                                   cur_node.end <= ovl_node.end) {
                            // tail of cur_node with head of prev_node
                            iter_start_idx = ovl_node.start;
                            iter_end_idx = cur_node.end;
                        } else if (ovl_node.start <= cur_node.start && cur_node.start <= ovl_node.end &&
                                   cur_node.end > ovl_node.end) {
                            // head of cur_node with tail of prev_node
                            iter_start_idx = cur_node.start;
                            iter_end_idx = ovl_node.end;
                        } else {
                            // overlapped within search window size, use euclidean distance directly
                            if (ovl_node.end < cur_node.start) {
                                if (nodeDistance(cur_node, ovl_node, range_mat_[channel][cur_node.start],
                                                 range_mat_[l][ovl_node.end])) {
                                    // if (DEBUG) {
                                    //     //printf("Merge by euclidean distance: cur: %f;%f;%f, prev: %f;%f;%f\n",
                                    //             range_mat_[channel][cur_node.start].x, range_mat_[channel][cur_node.start].y, range_mat_[channel][cur_node.start].z,
                                    //             range_mat_[l][ovl_node.end].x, range_mat_[l][ovl_node.end].y, range_mat_[l][ovl_node.end].z);
                                    // }
                                }
                            } else if (cur_node.end < ovl_node.start) {
                                if (nodeDistance(cur_node, ovl_node, range_mat_[channel][cur_node.end],
                                                 range_mat_[l][ovl_node.start])) {
                                    // if (DEBUG) {
                                    //     //printf("Merge by euclidean distance: cur: %f;%f;%f, prev: %f;%f;%f\n",
                                    //             range_mat_[channel][cur_node.end].x, range_mat_[channel][cur_node.end].y, range_mat_[channel][cur_node.end].z,
                                    //             range_mat_[l][ovl_node.start].x, range_mat_[l][ovl_node.start].y, range_mat_[l][ovl_node.start].z);
                                    // }
                                }
                            }
                            continue;
                        }

                        // if (DEBUG)
                        //     //printf("overlapping: %d %d\n", iter_start_idx, iter_end_idx);

                        // iterate through overlapping indices
                        uint16_t cur_label = 0, target_label = 0;
                        bool merged = false;
                        int cur_start_left = iter_start_idx;
                        int cur_start_right = iter_start_idx;
                        int cur_end_left = iter_end_idx;
                        int cur_end_right = iter_end_idx;

                        while (1) {
                            if (cur_start_right > cur_end_left && cur_start_left < iter_start_idx - HORZ_SCAN_SIZE &&
                                cur_end_right > iter_end_idx + HORZ_SCAN_SIZE) // end of search
                                break;
                            if (mergeNodes(cur_node, ovl_node, channel, l, cur_start_left))
                                break;
                            if (cur_start_left != cur_end_left) { // more than one overlapping cur_node point
                                if (mergeNodes(cur_node, ovl_node, channel, l, cur_end_left))
                                    break;
                            }
                            if (cur_start_left != cur_start_right) { // not the first iteration
                                if (mergeNodes(cur_node, ovl_node, channel, l, cur_start_right))
                                    break;
                            }
                            if (cur_end_left != cur_end_right) { // not the first iteration
                                if (mergeNodes(cur_node, ovl_node, channel, l, cur_end_right))
                                    break;
                            }
                            cur_start_left--;
                            cur_start_right++;
                            cur_end_left--;
                            cur_end_right++;
                        }
                    }
                }
            }
        }

        bool overlap(AOSNode &first_node, AOSNode &second_node) {
            int new_start = first_node.start - HORZ_EXTENSION_SIZE;
            int new_end = first_node.end + HORZ_EXTENSION_SIZE;
            if (new_start <= second_node.start && second_node.start <= new_end)
                return true;
            else if (new_start <= second_node.end && second_node.end <= new_end)
                return true;
            else if (second_node.start <= new_start && new_end <= second_node.end)
                return true;
            return false;
        }

        bool mergeNodes(AOSNode &first_node, AOSNode &second_node, int cur_channel, int prev_channel, int query_idx) {
            if (query_idx >= first_node.start && query_idx <= first_node.end) {
                if (range_mat_[cur_channel][query_idx].valid) {
                    int left_idx = query_idx;
                    int right_idx = query_idx;
                    while (1) {
                        if (left_idx <= query_idx - HORZ_SCAN_SIZE && right_idx >= query_idx + HORZ_SCAN_SIZE)
                            break;

                        if (left_idx >= second_node.start && left_idx <= second_node.end &&
                            range_mat_[prev_channel][left_idx].valid) {
                            if (nodeDistance(first_node, second_node, range_mat_[cur_channel][query_idx],
                                             range_mat_[prev_channel][left_idx])) {
                                // if (DEBUG)
                                //     //printf("query: %d, left_idx: %d\n", query_idx, left_idx);
                                return true;
                            }
                        }

                        if (right_idx <= second_node.end && right_idx >= second_node.start &&
                            range_mat_[prev_channel][right_idx].valid) {
                            if (nodeDistance(first_node, second_node, range_mat_[cur_channel][query_idx],
                                             range_mat_[prev_channel][right_idx])) {
                                // if (DEBUG)
                                //     //printf("query: %d, right_idx: %d\n", query_idx, right_idx);
                                return true;
                            }
                        }
                        left_idx--;
                        right_idx++;
                    }
                }
            }
            return false;
        }

        float pointDistance(Point pt1, Point pt2) {
            return sqrt((pt1.x - pt2.x) * (pt1.x - pt2.x) + (pt1.y - pt2.y) * (pt1.y - pt2.y));
        }

        bool nodeDistance(AOSNode &first_node, AOSNode &second_node, Point &first_point, Point &second_point) {
            uint16_t cur_label, target_label = 0;

            if (first_node.label == second_node.label)
                return false;

            if (pointDistance(first_point, second_point) < VERT_MERGE_THRES) {
                // if (DEBUG) {
                //     //printf("cur: %f;%f;%f, prev: %f;%f;%f distance: %f\n", first_point.x, first_point.y, first_point.z, second_point.x, second_point.y, second_point.z, pointDistance(first_point, second_point));
                //     //printf("Two nodes merged: %d, %d\n", first_node.label, second_node.label);
                // }

                if (first_node.label > second_node.label) {
                    cur_label = first_node.label;
                    target_label = second_node.label;
                    first_node.label = target_label;
                } else {
                    cur_label = second_node.label;
                    target_label = first_node.label;
                    second_node.label = target_label;
                }
                mergeClusters(cur_label, target_label);
                return true;
            }
            return false;
        }

        float getRange(T pt) {
            return sqrt(pt.x * pt.x + pt.y * pt.y + pt.z * pt.z);
        }

        int getRowIdx(T pt) {
            float angle = atan2(pt.z, sqrt(pt.x * pt.x + pt.y * pt.y)) * 180 / M_PI;
            auto iter_geq = std::lower_bound(vert_angles_.begin(), vert_angles_.end(), angle);
            int row_idx;

            if (iter_geq == vert_angles_.begin()) {
                row_idx = 0;
            } else {
                float a = *(iter_geq - 1);
                float b = *(iter_geq);
                if (fabs(angle - a) < fabs(angle - b)) {
                    row_idx = iter_geq - vert_angles_.begin() - 1;
                } else {
                    row_idx = iter_geq - vert_angles_.begin();
                }
            }
            return row_idx;
        }

        int getRowIdx(T pt, vector<float> vert_angles) {
            float angle = atan2(pt.z, sqrt(pt.x * pt.x + pt.y * pt.y)) * 180 / M_PI;
            auto iter_geq = std::lower_bound(vert_angles.begin(), vert_angles.end(), angle);
            int row_idx;

            if (iter_geq == vert_angles.begin()) {
                row_idx = 0;
            } else {
                float a = *(iter_geq - 1);
                float b = *(iter_geq);
                if (fabs(angle - a) < fabs(angle - b)) {
                    row_idx = iter_geq - vert_angles.begin() - 1;
                } else {
                    row_idx = iter_geq - vert_angles.begin();
                }
            }
            return row_idx;
        }

        int getColIdx(T pt) {
            float horizonAngle = atan2(pt.x, pt.y) * 180 / M_PI;
            static float ang_res_x = 360.0 / float(HORZ_SCAN);
            int col_idx = -round((horizonAngle - 90.0) / ang_res_x) + HORZ_SCAN / 2;
            if (col_idx >= HORZ_SCAN)
                col_idx -= HORZ_SCAN;
            return col_idx;
        }

        void mergeClusters(uint16_t cur_label, uint16_t target_label) {
            if (cur_label == 0 || target_label == 0) {
                //printf("Error merging runs cur_label:%u target_label:%u", cur_label, target_label);
            }
            for (auto &p: clusters_[cur_label]) {
                p->label = target_label;
            }
            clusters_[target_label].insert_after(clusters_[target_label].cbefore_begin(), clusters_[cur_label].begin(),
                                                 clusters_[cur_label].end());
            clusters_[cur_label].clear();
        }
    };


}

#endif