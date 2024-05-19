#include "utils/config.h"
#include <iostream>
#include <string>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include "datasets/datasets_init.h"
#include "back_end/reglib.h"
#include "global_definition/global_definition.h"
#include <pcl/common/transforms.h>
#include "robot_utils/file_manager.h"
#include <pcl/visualization/pcl_visualizer.h>
#include "front_end/gem/ellipsoid.h"
#include "front_end/gem/lineplane_extractor.h"

using namespace std;
using namespace clique_solver;
using namespace g3reg;

//typedef std::tuple<int, int, int> Color;
//std::map<int, Color> getSemanticColorMap(){
//    std::map<int, Color> instance_color_map;
//    std::set<int> ins_id_set = std::set<int>(instance_ids.begin(), instance_ids.end());
//    instance_color_map[-1] = Color(0, 0, 0);
//    for (int id: ins_id_set) {
//        if (id < 0) continue;
//        std::mt19937 rng(std::random_device{}());
//        std::uniform_int_distribution<int> dist(0, 255);
//        int r = dist(rng);
//        int g = dist(rng);
//        int b = dist(rng);
//        instance_color_map[id] = Color(r, g, b);
//    }
//    return instance_color_map;
//}


template<typename PointT>
void visualizeCloud(typename pcl::PointCloud<PointT>::Ptr source,
                    std::vector<std::vector<g3reg::QuadricFeature::Ptr>> &ellipsoids) {
    pcl::visualization::PCLVisualizer viewer("");
    viewer.setBackgroundColor(255, 255, 255);

    pcl::visualization::PointCloudColorHandlerCustom<PointT> sourceColor(source, 255, 180, 0);
    viewer.addPointCloud<PointT>(source, sourceColor, "source");
    viewer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 2, "source");

    int id = 0;
    for (int type = 0; type < ellipsoids.size(); type++) {
        // line, plane, cluster
        for (int i = 0; i < ellipsoids[type].size(); i++) {
            auto &quadric = ellipsoids[type][i];
            std::mt19937 rng(std::random_device{}());
            std::uniform_int_distribution<int> dist(0, 255);
            int r = dist(rng), g = dist(rng), b = dist(rng);
            auto cloud = quadric->cloud();
            pcl::visualization::PointCloudColorHandlerCustom<pcl::PointXYZ> quadricColor(cloud, r, g, b);
            id++;
            viewer.addPointCloud<pcl::PointXYZ>(cloud, quadricColor, "quadric" + std::to_string(id));
            viewer.setPointCloudRenderingProperties(pcl::visualization::PCL_VISUALIZER_POINT_SIZE, 3.5,
                                                    "quadric" + std::to_string(id));
        }
    }

    viewer.addCoordinateSystem(1.0);
    viewer.initCameraParameters();
    // set camera position at the center of the point source
    viewer.setCameraPosition(source->points[source->size() / 2].x,
                             source->points[source->size() / 2].y,
                             source->points[source->size() / 2].z,
                             0, 0, 1);
    viewer.spin();
    viewer.close();
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cout << "Usage: reg_bm config_file pcd1" << std::endl;
        return -1;
    }
    std::string config_path = config::project_path + "/" + argv[1];
    InitGLOG(config_path, argv);
    config::readParameters(config_path, argv);
    pcl::PointCloud<pcl::PointXYZ>::Ptr source(new pcl::PointCloud<pcl::PointXYZ>);
    if (pcl::io::loadPCDFile<pcl::PointXYZ>(std::string(argv[2]), *source) == -1) //* load the file
    {
        PCL_ERROR ("Couldn't read file source.pcd \n");
        return (-1);
    }

    g3reg::FeatureSet feature_set;
    g3reg::PLCExtractor plc_extractor;
    config::num_clusters = 1000;
    config::num_lines = 1000;
    config::num_planes = 1000;
    plc_extractor.ExtractFeature(source, feature_set.lines, feature_set.planes, feature_set.clusters);
    std::vector<std::vector<g3reg::QuadricFeature::Ptr>> src_ellipsoids;
    TransformToEllipsoid(feature_set, src_ellipsoids);

    visualizeCloud<pcl::PointXYZ>(source, src_ellipsoids);

    return 0;
}