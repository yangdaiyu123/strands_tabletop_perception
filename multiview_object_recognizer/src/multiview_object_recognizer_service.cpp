#define EIGEN_YES_I_KNOW_SPARSE_MODULE_IS_NOT_STABLE_YET

#include "multiview_object_recognizer_service.h"
#include "geometry_msgs/Point32.h"
#include "geometry_msgs/Transform.h"
#include "std_msgs/Time.h"
#include "segmenter.h"

#include <pcl/keypoints/sift_keypoint.h>
#include <pcl/recognition/cg/geometric_consistency.h>
#include <pcl/registration/icp.h>
#include <pcl/search/kdtree.h>

#include <v4r/ORFramework/faat_3d_rec_framework_defines.h>
#include <v4r/ORFramework/sift_local_estimator.h>
#include <v4r/ORRecognition/hv_go_3D.h>
#include <v4r/ORRegistration/fast_icp_with_gc.h>
#include <v4r/ORUtils/miscellaneous.h>
#include <v4r/ORUtils/noise_models.h>
#include <v4r/ORUtils/noise_model_based_cloud_integration.h>
#include <v4r/ORUtils/pcl_visualization_utils.h>
#include <v4r/ORUtils/segmentation_utils.h>


bool multiviewGraph::visualize_output() const
{
    return visualize_output_;
}

void multiviewGraph::setVisualize_output(bool visualize_output)
{
    visualize_output_ = visualize_output;
}

double multiviewGraph::chop_at_z() const
{
    return chop_at_z_;
}

void multiviewGraph::setChop_at_z(double chop_at_z)
{
    chop_at_z_ = chop_at_z;
}

void multiviewGraph::setPSingleview_recognizer(const boost::shared_ptr<Recognizer> &value)
{
    pSingleview_recognizer_ = value;
}


cv::Ptr<SiftGPU> multiviewGraph::sift() const
{
    return sift_;
}

void multiviewGraph::setSift(const cv::Ptr<SiftGPU> &sift)
{
    sift_ = sift;
}

void multiviewGraph::
createBigPointCloudRecursive (Graph & grph, Vertex &vrtx_start, pcl::PointCloud<pcl::PointXYZRGB>::Ptr & pAccumulatedPCl)
{
    grph[vrtx_start].has_been_hopped_ = true;
    *pAccumulatedPCl += *(grph[vrtx_start].pScenePCl);

    typename graph_traits<Graph>::out_edge_iterator out_i, out_end;
    tie ( out_i, out_end ) = out_edges ( vrtx_start, grph);

    if(out_i != out_end)  //otherwise there are no edges to hop
    {   //------get hypotheses from next vertex. Just taking the first one not being hopped.----
        std::string edge_src, edge_trgt;
        Vertex remote_vertex;
        Eigen::Matrix4f edge_transform;

        bool have_found_a_valid_edge = false;
        for (; out_i != out_end; ++out_i )
        {
            remote_vertex  = target ( *out_i, grph );
            edge_src       = grph[*out_i].source_id;
            edge_trgt      = grph[*out_i].target_id;
            edge_transform = grph[*out_i].transformation;

            if (! grph[remote_vertex].has_been_hopped_)
            {
                have_found_a_valid_edge = true;
                std::cout << "Edge src: " << edge_src << "; target: " << edge_trgt << "; model_name: " << grph[*out_i].model_name
                          << "; edge_weight: " << grph[*out_i].edge_weight << std::endl;
                break;
            }
        }
        if(have_found_a_valid_edge)
        {
            PCL_INFO("Create big point cloud: Hopping to vertex %s...", grph[remote_vertex].pScenePCl->header.frame_id.c_str());
            pcl::PointCloud<pcl::PointXYZRGB>::Ptr pExtendedPCl (new pcl::PointCloud<pcl::PointXYZRGB>);

            Eigen::Matrix4f transform;
            if ( edge_src.compare( grph[vrtx_start].pScenePCl->header.frame_id ) == 0)
            {
                transform = edge_transform.inverse();
            }
            else if (edge_trgt.compare( grph[vrtx_start].pScenePCl->header.frame_id ) == 0)
            {
                transform = edge_transform;
            }

            //grph[remote_vertex].absolute_pose_ = grph[vrtx_start].absolute_pose_ * transform;
            createBigPointCloudRecursive(grph, remote_vertex, pExtendedPCl);

            pcl::PointCloud<pcl::PointXYZRGB>::Ptr pTransformedExtendPCl (new pcl::PointCloud<pcl::PointXYZRGB>);
            pcl::transformPointCloud (*pExtendedPCl, *pTransformedExtendPCl, transform);
            *pAccumulatedPCl += *pTransformedExtendPCl;
        }
    }
}


bool multiviewGraph::
calcSiftFeatures (Vertex &src, Graph &grph)
{
    boost::shared_ptr < faat_pcl::rec_3d_framework::SIFTLocalEstimation<PointT, FeatureT> > estimator;
    estimator.reset (new faat_pcl::rec_3d_framework::SIFTLocalEstimation<PointT, FeatureT>(sift_));

    //    if(use_table_plane)
    //        estimator->setIndices (*(grph[src].pIndices_above_plane));

    boost::shared_ptr< pcl::PointCloud<PointT> > pSiftKeypoints;
    bool ret = estimator->estimate (grph[src].pScenePCl_f, pSiftKeypoints, grph[src].pSiftSignatures_, grph[src].sift_keypoints_scales_);
    estimator->getKeypointIndices(grph[src].siftKeypointIndices_);

    return ret;

    //----display-keypoints--------------------
    /*pcl::visualization::PCLVisualizer::Ptr vis_temp (new pcl::visualization::PCLVisualizer);
     pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> handler_rgb_verified (grph[*it_vrtx].pScenePCl);
     vis_temp->addPointCloud<pcl::PointXYZRGB> (grph[*it_vrtx].pScenePCl, handler_rgb_verified, "Hypothesis_1");
     pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> handler_rgb_verified2 (grph[*it_vrtx].pSiftKeypoints);

     for (size_t keyId = 0; keyId < grph[*it_vrtx].pSiftKeypoints->size (); keyId++)
     {
     std::stringstream sphere_name;
     sphere_name << "sphere_" << keyId;
     vis_temp->addSphere<pcl::PointXYZRGB> (grph[*it_vrtx].pSiftKeypoints->at (keyId), 0.01, sphere_name.str ());
     }
     vis_temp->spin ();*/
}

void
multiview::nearestKSearch ( flann::Index<flann::L1<float> > * index, float * descr, int descr_size, int k, flann::Matrix<int> &indices,
                            flann::Matrix<float> &distances )
{
    flann::Matrix<float> p = flann::Matrix<float> ( new float[descr_size], 1, descr_size );
    memcpy ( &p.ptr () [0], &descr[0], p.cols * p.rows * sizeof ( float ) );

    index->knnSearch ( p, indices, distances, k, flann::SearchParams ( 128 ) );
    delete[] p.ptr ();
}

template<typename Type>
void
multiview::convertToFLANN ( const typename pcl::PointCloud<Type>::ConstPtr & cloud, flann::Matrix<float> &data )
{
    data.rows = cloud->points.size ();
    data.cols = sizeof ( cloud->points[0].histogram ) / sizeof ( float ); // number of histogram bins

    flann::Matrix<float> flann_data ( new float[data.rows * data.cols], data.rows, data.cols );

    for ( size_t i = 0; i < data.rows; ++i )
        for ( size_t j = 0; j < data.cols; ++j )
        {
            flann_data.ptr () [i * data.cols + j] = cloud->points[i].histogram[j];
        }

    data = flann_data;
}

template void
multiview::convertToFLANN<pcl::Histogram<128> > (const pcl::PointCloud<pcl::Histogram<128> >::ConstPtr & cloud, flann::Matrix<float> &data ); // explicit instantiation.


void multiviewGraph::
estimateViewTransformationBySIFT ( const Vertex &src, const Vertex &trgt, Graph &grph, flann::Index<DistT> *flann_index, Eigen::Matrix4f &transformation,
                                   std::vector<Edge> & edges, bool use_gc )
{
    const int K = 1;
    flann::Matrix<int> indices = flann::Matrix<int> ( new int[K], 1, K );
    flann::Matrix<float> distances = flann::Matrix<float> ( new float[K], 1, K );

    boost::shared_ptr< pcl::PointCloud<PointT> > pSiftKeypointsSrc (new pcl::PointCloud<PointT>);
    boost::shared_ptr< pcl::PointCloud<PointT> > pSiftKeypointsTrgt (new pcl::PointCloud<PointT>);
    pcl::copyPointCloud(*(grph[ src].pScenePCl_f), grph[ src].siftKeypointIndices_, *(pSiftKeypointsSrc ));
    pcl::copyPointCloud(*(grph[trgt].pScenePCl_f), grph[trgt].siftKeypointIndices_, *(pSiftKeypointsTrgt));
    PCL_INFO ( "Calculate transform via SIFT between view %s and %s for a keypoint size of %ld (src) and %ld (target).",
               grph[src].pScenePCl->header.frame_id.c_str(), grph[trgt].pScenePCl->header.frame_id.c_str(), pSiftKeypointsSrc->points.size(), pSiftKeypointsTrgt->points.size() );

    pcl::CorrespondencesPtr temp_correspondences ( new pcl::Correspondences );
    temp_correspondences->resize(pSiftKeypointsSrc->size ());

    for ( size_t keypointId = 0; keypointId < pSiftKeypointsSrc->size (); keypointId++ )
    {
        FeatureT searchFeature = grph[src].pSiftSignatures_->at ( keypointId );
        int size_feat = sizeof ( searchFeature.histogram ) / sizeof ( float );
        multiview::nearestKSearch ( flann_index, searchFeature.histogram, size_feat, K, indices, distances );

        pcl::Correspondence corr;
        corr.distance = distances[0][0];
        corr.index_query = keypointId;
        corr.index_match = indices[0][0];
        temp_correspondences->at(keypointId) = corr;
    }

    if(!use_gc)
    {
        pcl::registration::CorrespondenceRejectorSampleConsensus<PointT>::Ptr rej;
        rej.reset (new pcl::registration::CorrespondenceRejectorSampleConsensus<PointT> ());
        pcl::CorrespondencesPtr after_rej_correspondences (new pcl::Correspondences ());

        rej->setMaximumIterations (50000);
        rej->setInlierThreshold (0.02);
        rej->setInputTarget (pSiftKeypointsTrgt);
        rej->setInputSource (pSiftKeypointsSrc);
        rej->setInputCorrespondences (temp_correspondences);
        rej->getCorrespondences (*after_rej_correspondences);

        transformation = rej->getBestTransformation ();
        pcl::registration::TransformationEstimationSVD<PointT, PointT> t_est;
        t_est.estimateRigidTransformation (*pSiftKeypointsSrc, *pSiftKeypointsTrgt, *after_rej_correspondences, transformation);

        bool b;
        Edge edge;
        tie (edge, b) = add_edge (trgt, src, grph);
        grph[edge].transformation = transformation;
        grph[edge].model_name = std::string ("scene_to_scene");
        grph[edge].source_id = grph[src].pScenePCl->header.frame_id;
        grph[edge].target_id = grph[trgt].pScenePCl->header.frame_id;
        edges.push_back(edge);
    }
    else
    {
        pcl::GeometricConsistencyGrouping<pcl::PointXYZRGB, pcl::PointXYZRGB> gcg_alg;

        gcg_alg.setGCThreshold (15);
        gcg_alg.setGCSize (0.01);
        //        if( (pSiftKeypointsSrc->points.size() == grph[src].keypointIndices_.indices.size())
        //                && (pSiftKeypointsTrgt->points.size() == grph[trgt].keypointIndices_.indices.size()) )
        //        {
        //            std::cout << "we could use normals " << std::endl;
        //            pcl::PointCloud<pcl::Normal>::Ptr pInputNormals (new pcl::PointCloud<pcl::Normal>);
        //            pcl::PointCloud<pcl::Normal>::Ptr pSceneNormals (new pcl::PointCloud<pcl::Normal>);
        //            pcl::copyPointCloud(*(grph[ src].pSceneNormals_f_), grph[ src].keypointIndices_, *pInputNormals);
        //            pcl::copyPointCloud(*(grph[trgt].pSceneNormals_f_), grph[trgt].keypointIndices_, *pSceneNormals);
        //            gcg_alg.setInputAndSceneNormals(pInputNormals, pSceneNormals);
        //        }
        gcg_alg.setInputCloud(pSiftKeypointsSrc);
        gcg_alg.setSceneCloud(pSiftKeypointsTrgt);
        gcg_alg.setModelSceneCorrespondences(temp_correspondences);

        std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f> > transformations;
        std::vector<pcl::Correspondences> clustered_corrs;
        gcg_alg.recognize(transformations, clustered_corrs);

        for(size_t i=0; i < transformations.size(); i++)
        {
            PointInTPtr transformed_PCl (new pcl::PointCloud<pcl::PointXYZRGB>);
            pcl::transformPointCloud (*grph[src].pScenePCl, *transformed_PCl, transformations[i]);

            std::stringstream scene_stream;
            scene_stream << "scene_to_scene_cg_" << i;
            bool b;
            Edge edge;
            tie (edge, b) = add_edge (trgt, src, grph);
            grph[edge].transformation = transformations[i];
            grph[edge].model_name = scene_stream.str();
            grph[edge].source_id = grph[src].pScenePCl->header.frame_id;
            grph[edge].target_id = grph[trgt].pScenePCl->header.frame_id;
            edges.push_back(edge);
        }
    }
}

/*
 * If a global_transform for the point cloud is provided as an argument for the recognition
 * service and use_robot_pose is set to true, additional edges between each views (vertices) are created.
 */
void multiviewGraph::
estimateViewTransformationByRobotPose ( const Vertex &src, const Vertex &trgt, Graph &grph, Edge &edge )
{
    bool b;
    tie ( edge, b ) = add_edge ( trgt, src, grph );
    Eigen::Matrix4f tf2wco_src = grph[src].transform_to_world_co_system_;
    Eigen::Matrix4f tf2wco_trgt = grph[trgt].transform_to_world_co_system_;
    grph[edge].transformation = tf2wco_trgt.inverse() * tf2wco_src;
    grph[edge].model_name = std::string ( "robot_pose" );
    grph[edge].source_id = grph[src].pScenePCl->header.frame_id;
    grph[edge].target_id = grph[trgt].pScenePCl->header.frame_id;
}


/*
 * Transfers keypoints and hypotheses from other views (vertices) in the graph and
 * adds/merges them to the current keyppoints (existing) ones.
 * Correspondences with keypoints close to existing ones (distance, normal and model id) are
 * not transferred (to avoid redundant correspondences)
 */
void multiviewGraph::
extendFeatureMatchesRecursive ( Graph &grph,
                                Vertex &vrtx_start,
                                std::map < std::string,faat_pcl::rec_3d_framework::ObjectHypothesis<PointT> > &hypotheses,
                                pcl::PointCloud<PointT>::Ptr pKeypoints,
                                pcl::PointCloud<pcl::Normal>::Ptr pKeypointNormals)
{
    pcl::copyPointCloud(*(grph[vrtx_start].pKeypointsMultipipe_), *pKeypoints);
    pcl::copyPointCloud(*(grph[vrtx_start].pKeypointNormalsMultipipe_), *pKeypointNormals);

    std::map<std::string, faat_pcl::rec_3d_framework::ObjectHypothesis<PointT> >::const_iterator it_copy_hyp;
    for(it_copy_hyp = grph[vrtx_start].hypotheses_.begin(); it_copy_hyp !=grph[vrtx_start].hypotheses_.end(); ++it_copy_hyp)
    {
        faat_pcl::rec_3d_framework::ObjectHypothesis<PointT> oh;
        oh.model_ = it_copy_hyp->second.model_;
        oh.correspondences_pointcloud.reset(new pcl::PointCloud<PointT>(*(it_copy_hyp->second.correspondences_pointcloud)));
        oh.normals_pointcloud.reset(new pcl::PointCloud<pcl::Normal>(*(it_copy_hyp->second.normals_pointcloud)));
        oh.correspondences_to_inputcloud.reset(new pcl::Correspondences);
        *(oh.correspondences_to_inputcloud) = *(it_copy_hyp->second.correspondences_to_inputcloud);
        oh.indices_to_flann_models_ = it_copy_hyp->second.indices_to_flann_models_;
        hypotheses.insert(std::pair<std::string, faat_pcl::rec_3d_framework::ObjectHypothesis<PointT> >(it_copy_hyp->first, oh));
    }

    assert(pKeypoints->points.size() == pKeypointNormals->points.size());

    grph[vrtx_start].has_been_hopped_ = true;

    typename graph_traits<Graph>::out_edge_iterator out_i, out_end;
    tie ( out_i, out_end ) = out_edges ( vrtx_start, grph);
    if(out_i != out_end)  //otherwise there are no edges to hop
    {   //------get hypotheses from next vertex. Just taking the first one not being hopped.----
        std::string edge_src, edge_trgt;
        Vertex remote_vertex;
        Eigen::Matrix4f edge_transform;

        for (; out_i != out_end; ++out_i )
        {
            remote_vertex  = target ( *out_i, grph );
            edge_src       = grph[*out_i].source_id;
            edge_trgt      = grph[*out_i].target_id;
            edge_transform = grph[*out_i].transformation;

            if (! grph[remote_vertex].has_been_hopped_)
            {
                std::cout << "Edge src: " << edge_src << "; target: " << edge_trgt << "; model_name: " << grph[*out_i].model_name
                          << "; edge_weight: " << grph[*out_i].edge_weight << std::endl;

                if ( edge_src.compare( grph[vrtx_start].pScenePCl->header.frame_id ) == 0)
                {
                    edge_transform = grph[*out_i].transformation.inverse();
                }

                PCL_INFO("Hopping to vertex %s...", grph[remote_vertex].pScenePCl->header.frame_id.c_str());

                std::map<std::string, faat_pcl::rec_3d_framework::ObjectHypothesis<PointT> > new_hypotheses;
                pcl::PointCloud<PointT>::Ptr pNewKeypoints (new pcl::PointCloud<PointT>);
                pcl::PointCloud<pcl::Normal>::Ptr pNewKeypointNormals (new pcl::PointCloud<pcl::Normal>);
                grph[remote_vertex].absolute_pose_ = grph[vrtx_start].absolute_pose_ * edge_transform;
                extendFeatureMatchesRecursive ( grph, remote_vertex, new_hypotheses, pNewKeypoints, pNewKeypointNormals);
                assert( pNewKeypoints->size() == pNewKeypointNormals->size() );

                //------ Transform keypoints and rotate normals----------
                const Eigen::Matrix3f rot   = edge_transform.block<3, 3> (0, 0);
                const Eigen::Vector3f trans = edge_transform.block<3, 1> (0, 3);
                for (size_t i=0; i<pNewKeypoints->size(); i++)
                {
                    pNewKeypoints->points[i].getVector3fMap() = rot * pNewKeypoints->points[i].getVector3fMap () + trans;
                    pNewKeypointNormals->points[i].getNormalVector3fMap() = rot * pNewKeypointNormals->points[i].getNormalVector3fMap ();
                }

                // add/merge hypotheses
                pcl::PointCloud<pcl::PointXYZ>::Ptr pKeypointsXYZ (new pcl::PointCloud<pcl::PointXYZ>);
                pKeypointsXYZ->points.resize(pKeypoints->size());
                for(size_t i=0; i < pKeypoints->size(); i++)
                {
                    pKeypointsXYZ->points[i].getVector3fMap() = pKeypoints->points[i].getVector3fMap();
                }

                std::map<std::string, faat_pcl::rec_3d_framework::ObjectHypothesis<PointT> >::const_iterator it_new_hyp;
                for(it_new_hyp = new_hypotheses.begin(); it_new_hyp !=new_hypotheses.end(); ++it_new_hyp)
                {
                    const std::string id = it_new_hyp->second.model_->id_;

                    std::map<std::string, faat_pcl::rec_3d_framework::ObjectHypothesis<PointT> >::iterator it_existing_hyp;
                    it_existing_hyp = hypotheses.find(id);
                    if (it_existing_hyp == hypotheses.end())
                    {
                        PCL_ERROR("There are no hypotheses (feature matches) for this model yet.");
                        faat_pcl::rec_3d_framework::ObjectHypothesis<PointT> oh;
                        oh.correspondences_pointcloud.reset(new pcl::PointCloud<PointT>
                                                            (*(it_new_hyp->second.correspondences_pointcloud)));
                        oh.normals_pointcloud.reset(new pcl::PointCloud<pcl::Normal>
                                                    (*(it_new_hyp->second.normals_pointcloud)));
                        oh.indices_to_flann_models_ = it_new_hyp->second.indices_to_flann_models_;
                        oh.correspondences_to_inputcloud.reset (new pcl::Correspondences);
                        *(oh.correspondences_to_inputcloud) = *(it_new_hyp->second.correspondences_to_inputcloud);
                        for(size_t i=0; i < oh.correspondences_to_inputcloud->size(); i++)
                        {
                            oh.correspondences_to_inputcloud->at(i).index_match += pKeypoints->points.size();
                        }
                        hypotheses.insert(std::pair<std::string, faat_pcl::rec_3d_framework::ObjectHypothesis<PointT> >(id, oh));
                    }
                    else
                    { //merge hypotheses
                        for(size_t j=0; j < it_new_hyp->second.correspondences_to_inputcloud->size(); j++)
                        {
                            bool drop_new_correspondence = false;

                            pcl::Correspondence c_new = it_new_hyp->second.correspondences_to_inputcloud->at(j);
                            const PointT new_kp = pNewKeypoints->points[c_new.index_match];
                            pcl::PointXYZ new_kp_XYZ;
                            new_kp_XYZ.getVector3fMap() = new_kp.getVector3fMap();
                            const pcl::Normal new_kp_normal = pNewKeypointNormals->points[c_new.index_match];

                            const PointT new_model_pt = it_new_hyp->second.correspondences_pointcloud->points[ c_new.index_query ];
                            pcl::PointXYZ new_model_pt_XYZ;
                            new_model_pt_XYZ.getVector3fMap() = new_model_pt.getVector3fMap();
                            const pcl::Normal new_model_normal = it_new_hyp->second.normals_pointcloud->points[ c_new.index_query ];

                            if (!pcl::isFinite(new_kp_XYZ) || !pcl::isFinite(new_model_pt_XYZ))
                            {
                                PCL_WARN("Keypoint of scene or model is infinity!!");
                                continue;
                            }
                            for(size_t j=0; j < it_existing_hyp->second.correspondences_to_inputcloud->size(); j++)
                            {
                                const pcl::Correspondence c_existing = it_existing_hyp->second.correspondences_to_inputcloud->at(j);
                                const PointT existing_model_pt = it_existing_hyp->second.correspondences_pointcloud->points[ c_existing.index_query ];
                                pcl::PointXYZ existing_model_pt_XYZ;
                                existing_model_pt_XYZ.getVector3fMap() = existing_model_pt.getVector3fMap();

                                const PointT existing_kp = pKeypoints->points[c_existing.index_match];
                                pcl::PointXYZ existing_kp_XYZ;
                                existing_kp_XYZ.getVector3fMap() = existing_kp.getVector3fMap();
                                const pcl::Normal existing_kp_normal = pKeypointNormals->points[c_existing.index_match];

                                float squaredDistModelKeypoints = pcl::squaredEuclideanDistance(new_model_pt_XYZ, existing_model_pt_XYZ);
                                float squaredDistSceneKeypoints = pcl::squaredEuclideanDistance(new_kp_XYZ, existing_kp_XYZ);

                                if((squaredDistSceneKeypoints < distance_keypoints_get_discarded_) &&
                                        (new_kp_normal.getNormalVector3fMap().dot(existing_kp_normal.getNormalVector3fMap()) > 0.8) &&
                                        (squaredDistModelKeypoints < distance_keypoints_get_discarded_))
                                {
                                    //                                std::cout << "Found a very close point (keypoint distance: " << squaredDistSceneKeypoints
                                    //                                          << "; model distance: " << squaredDistModelKeypoints
                                    //                                          << ") with the same model id and similar normal (Normal dot product: "
                                    //                                          << new_kp_normal.getNormalVector3fMap().dot(existing_kp_normal.getNormalVector3fMap()) << "> 0.8). Ignoring it."
                                    //                                                                      << std::endl;
                                    drop_new_correspondence = true;
                                    break;
                                }
                            }
                            if (!drop_new_correspondence)
                            {
                                it_existing_hyp->second.indices_to_flann_models_.push_back(
                                            it_new_hyp->second.indices_to_flann_models_[c_new.index_query]); //check that
                                c_new.index_query = it_existing_hyp->second.correspondences_pointcloud->points.size();
                                c_new.index_match += pKeypoints->points.size();
                                it_existing_hyp->second.correspondences_to_inputcloud->push_back(c_new);
                                it_existing_hyp->second.correspondences_pointcloud->points.push_back(new_model_pt);
                                it_existing_hyp->second.normals_pointcloud->points.push_back(new_model_normal);
                            }
                        }
                        //                    std::cout << "INFO: Size for " << id <<
                        //                                 " of correspondes_pointcloud after merge: " << it_existing_hyp->second.correspondences_pointcloud->points.size() << std::endl;
                    }
                }
                *pKeypoints += *pNewKeypoints;
                *pKeypointNormals += *pNewKeypointNormals;
            }
        }
    }
}

/*
 * Extends hypotheses construced from other views in graph by following "calling_out_edge" and recursively the other views
 */
void multiviewGraph::
extendHypothesisRecursive ( Graph &grph, Vertex &vrtx_start, std::vector<Hypothesis<PointT> > &hyp_vec, bool use_unverified_hypotheses) //is directed edge (so the source of calling_edge is calling vertex)
{
    grph[vrtx_start].has_been_hopped_ = true;

    for ( std::vector<Hypothesis<PointT>>::const_iterator it_hyp = grph[vrtx_start].hypothesis_mv_.begin (); it_hyp != grph[vrtx_start].hypothesis_mv_.end (); ++it_hyp )
    {
        if(!it_hyp->verified_ && !use_unverified_hypotheses)
            continue;

        bool id_already_exists = false;
        //--check if hypothesis already exists
        for ( std::vector<Hypothesis<PointT>>::const_iterator it_existing_hyp = hyp_vec.begin ();
              it_existing_hyp != hyp_vec.end ();
              ++it_existing_hyp )
        {
            if( it_hyp->id_ == it_existing_hyp->id_)
            {
                id_already_exists = true;
                break;
            }
        }
        if(!id_already_exists)
        {
            Hypothesis<PointT> ht_temp ( it_hyp->model_, it_hyp->transform_, it_hyp->origin_, false, false, it_hyp->id_ );
            hyp_vec.push_back(ht_temp);
        }
    }

    typename graph_traits<Graph>::out_edge_iterator out_i, out_end;
    tie ( out_i, out_end ) = out_edges ( vrtx_start, grph);
    if(out_i != out_end)  //otherwise there are no edges to hop
    {   //------get hypotheses from next vertex. Just taking the first one not being hopped.----
        std::string edge_src, edge_trgt;
        Vertex remote_vertex;
        Eigen::Matrix4f edge_transform;

        for (; out_i != out_end; ++out_i )
        {
            remote_vertex  = target ( *out_i, grph );
            edge_src       = grph[*out_i].source_id;
            edge_trgt      = grph[*out_i].target_id;
            edge_transform = grph[*out_i].transformation;

            if (! grph[remote_vertex].has_been_hopped_)
            {
                grph[remote_vertex].cumulative_weight_to_new_vrtx_ = grph[vrtx_start].cumulative_weight_to_new_vrtx_ + grph[*out_i].edge_weight;

                std::cout << "Edge src: " << edge_src << "; target: " << edge_trgt << "; model_name: " << grph[*out_i].model_name
                          << "; edge_weight: " << grph[*out_i].edge_weight << std::endl;

                if ( edge_src.compare( grph[vrtx_start].pScenePCl->header.frame_id ) == 0)
                {
                    edge_transform = grph[*out_i].transformation.inverse();
                }

                PCL_INFO("Hopping to vertex %s which has a cumulative weight of %lf",
                         grph[remote_vertex].pScenePCl->header.frame_id.c_str(),
                         grph[remote_vertex].cumulative_weight_to_new_vrtx_);

                std::vector<Hypothesis<PointT> > new_hypotheses;
                grph[remote_vertex].absolute_pose_ = grph[vrtx_start].absolute_pose_ * edge_transform;
                extendHypothesisRecursive ( grph, remote_vertex, new_hypotheses);

                for(std::vector<Hypothesis<PointT> >::iterator it_new_hyp = new_hypotheses.begin(); it_new_hyp !=new_hypotheses.end(); ++it_new_hyp)
                {
                    it_new_hyp->transform_ = edge_transform * it_new_hyp->transform_;
                    Hypothesis<PointT> ht_temp ( it_new_hyp->model_, it_new_hyp->transform_, it_new_hyp->origin_, true, false, it_new_hyp->id_ );
                    hyp_vec.push_back(ht_temp);
                }
            }
        }
    }
}

/*
 * Connects a new view to the graph by edges sharing a common object hypothesis
 */
void multiviewGraph::
createEdgesFromHypothesisMatchOnline ( const Vertex new_vertex, Graph &grph, std::vector<Edge> &edges )
{
    vertex_iter vertexItA, vertexEndA;
    for (boost::tie(vertexItA, vertexEndA) = vertices(grph_); vertexItA != vertexEndA; ++vertexItA)
    {
        if ( grph[*vertexItA].pScenePCl->header.frame_id.compare( grph[new_vertex].pScenePCl->header.frame_id ) == 0 )
        {
            continue;
        }

        PCL_INFO("Checking vertex %s, which has %ld hypotheses.", grph[*vertexItA].pScenePCl->header.frame_id.c_str(), grph[*vertexItA].hypothesis_sv_.size());
        for ( std::vector<Hypothesis<PointT> >::iterator it_hypA = grph[*vertexItA].hypothesis_sv_.begin (); it_hypA != grph[*vertexItA].hypothesis_sv_.end (); ++it_hypA )
        {
            for ( std::vector<Hypothesis<PointT> >::iterator it_hypB = grph[new_vertex].hypothesis_sv_.begin (); it_hypB != grph[new_vertex].hypothesis_sv_.end (); ++it_hypB )
            {
                if ( it_hypB->model_id_.compare (it_hypA->model_id_ ) == 0 ) //model exists in other file (same id) --> create connection
                {
                    Eigen::Matrix4f tf_temp = it_hypB->transform_ * it_hypA->transform_.inverse (); //might be the other way around

                    //link views by an edge (for other graph)
                    Edge e_cpy;
                    bool b;
                    tie ( e_cpy, b ) = add_edge ( *vertexItA, new_vertex, grph );
                    grph[e_cpy].transformation = tf_temp;
                    grph[e_cpy].model_name = it_hypA->model_id_;
                    grph[e_cpy].source_id = grph[*vertexItA].pScenePCl->header.frame_id;
                    grph[e_cpy].target_id = grph[new_vertex].pScenePCl->header.frame_id;
                    grph[e_cpy].edge_weight = std::numeric_limits<double>::max ();
                    edges.push_back ( e_cpy );

                    std::cout << "Creating edge from view " << grph[*vertexItA].pScenePCl->header.frame_id << " to view " << grph[new_vertex].pScenePCl->header.frame_id
                              << " for model match " << grph[e_cpy].model_name
                              << std::endl;
                }
            }
        }
    }
}

void multiviewGraph::
calcEdgeWeight (Graph &grph, std::vector<Edge> &edges)
{
#pragma omp parallel for
    for (size_t i=0; i<edges.size(); i++) //std::vector<Edge>::iterator edge_it = edges.begin(); edge_it!=edges.end(); ++edge_it)
    {
        Edge edge = edges[i];

        const Vertex vrtx_src = source ( edge, grph );
        const Vertex vrtx_trgt = target ( edge, grph );

        Eigen::Matrix4f transform;
        if ( grph[edge].source_id.compare( grph[vrtx_src].pScenePCl->header.frame_id ) == 0)
        {
            transform = grph[edge].transformation;
        }
        else
        {
            transform = grph[edge].transformation.inverse ();
        }

        float w_after_icp_ = std::numeric_limits<float>::max ();
        const float best_overlap_ = 0.75f;

        Eigen::Matrix4f icp_trans;
        faat_pcl::registration::FastIterativeClosestPointWithGC<pcl::PointXYZRGB> icp;
        icp.setMaxCorrespondenceDistance ( 0.02f );
        icp.setInputSource (grph[vrtx_src].pScenePCl_f);
        icp.setInputTarget (grph[vrtx_trgt].pScenePCl_f);
        icp.setUseNormals (true);
        icp.useStandardCG (true);
        icp.setNoCG(true);
        icp.setOverlapPercentage (best_overlap_);
        icp.setKeepMaxHypotheses (5);
        icp.setMaximumIterations (10);
        icp.align (transform);
        w_after_icp_ = icp.getFinalTransformation ( icp_trans );

        if ( w_after_icp_ < 0 || !pcl_isfinite ( w_after_icp_ ) )
        {
            w_after_icp_ = std::numeric_limits<float>::max ();
        }
        else
        {
            w_after_icp_ = best_overlap_ - w_after_icp_;
        }

        if ( grph[edge].source_id.compare( grph[vrtx_src].pScenePCl->header.frame_id ) == 0)
        {
            PCL_WARN ( "Normal...\n" );
            //icp trans is aligning source to target
            //transform is aligning source to target
            //grph[edges[edge_id]].transformation = icp_trans * grph[edges[edge_id]].transformation;
            grph[edge].transformation = icp_trans;
        }
        else
        {
            //transform is aligning target to source
            //icp trans is aligning source to target
            PCL_WARN ( "Inverse...\n" );
            //grph[edges[edge_id]].transformation = icp_trans.inverse() * grph[edges[edge_id]].transformation;
            grph[edge].transformation = icp_trans.inverse ();
        }

        grph[edge].edge_weight = w_after_icp_;

        std::cout << "WEIGHT IS: " << grph[edge].edge_weight << " coming from edge connecting " << grph[edge].source_id
                  << " and " << grph[edge].target_id << " by object_id: " << grph[edge].model_name
                  << std::endl;
    }
}

Eigen::Matrix4f GeometryMsgToMatrix4f(const geometry_msgs::Transform & tt)
{
    Eigen::Matrix4f trans = Eigen::Matrix4f::Identity();
    trans(0,3) = tt.translation.x;
    trans(1,3) = tt.translation.y;
    trans(2,3) = tt.translation.z;

    Eigen::Quaternionf q(tt.rotation.w,tt.rotation.x,tt.rotation.y,tt.rotation.z);
    trans.block<3,3>(0,0) = q.toRotationMatrix();
    return trans;
}


/*
 * recognition passing a global transformation matrix (to a world coordinate system)
 */
bool multiviewGraph::recognize
(const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr pInputCloud,
 const std::string view_name,
 const size_t timestamp,
 const Eigen::Matrix4f global_transform)
{
    use_robot_pose_ = true;
    current_global_transform_ = global_transform;
    recognize(pInputCloud, view_name, timestamp);
}


bool multiviewGraph::recognize
(const pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr pInputCloud,
 const std::string view_name,
 const size_t timestamp)
{
    std::cout << "=================================================================" << std::endl <<
                 "Started recognition for view " << view_name << " in scene " << scene_name_ << " with timestamp " << timestamp <<
                 " and following settings for mv_recognizer: " << std::endl <<
                 "***Max vertices kept in graph: " << max_vertices_in_graph_ << std::endl <<
                 "***euclidean distance keypoints get discarded during extension: " << distance_keypoints_get_discarded_ << std::endl <<
                 "***chop at z: " << chop_at_z_ << std::endl <<
                 "***scene to scene calculation: " << scene_to_scene_ << std::endl <<
                 "***robot pose for transform calculation: " << use_robot_pose_ << std::endl <<
                 "=========================================================" << std::endl << std::endl;

    double  total_time,
            sv_hyp_construction_time,
            sv_overhead_time,
            mv_hyp_construct_time,
            mv_hyp_ver_time,
            mv_feat_ext_time,
            mv_icp_time,
            sv_total_time;

    size_t total_num_correspondences=0;

    times_.clear();

    pcl::ScopeTime total_pcl_time ("Multiview Recognition");

    boost::shared_ptr< pcl::PointCloud<pcl::Normal> > pSceneNormals_f (new pcl::PointCloud<pcl::Normal> );

    assert(pInputCloud->width == 640 && pInputCloud->height == 480);    // otherwise the recognition library does not work
    Vertex vrtx = boost::add_vertex ( grph_ );

    pcl::copyPointCloud(*pInputCloud, *(grph_[vrtx].pScenePCl));
    //    grph_[vrtx].pScenePCl = pInputCloud;
    grph_[vrtx].pScenePCl->header.stamp = timestamp;
    grph_[vrtx].pScenePCl->header.frame_id = view_name;
    grph_[vrtx].absolute_pose_ = Eigen::Matrix4f::Identity();

    most_current_view_id_ = view_name;

    if(use_robot_pose_)
        grph_[vrtx].transform_to_world_co_system_ = current_global_transform_;

    if(chop_at_z_ > 0)
    {
        pcl::PassThrough<PointT> pass;
        pass.setFilterLimits (0.f, chop_at_z_);
        pass.setFilterFieldName ("z");
        pass.setInputCloud (grph_[vrtx].pScenePCl);
        pass.setKeepOrganized (true);
        pass.filter (*(grph_[vrtx].pScenePCl_f));
        grph_[vrtx].filteredSceneIndices_.indices = *(pass.getIndices());
        grph_[vrtx].filteredSceneIndices_.header.stamp = timestamp;
        grph_[vrtx].filteredSceneIndices_.header.frame_id = view_name;
    }

    //-----Normal estimation ---------------
    pcl::NormalEstimationOMP<PointT, pcl::Normal> ne;
    ne.setRadiusSearch ( 0.02f );
    ne.setInputCloud ( grph_[vrtx].pScenePCl );
    ne.compute ( *(grph_[vrtx].pSceneNormals) );

    pcl::copyPointCloud(*(grph_[vrtx].pSceneNormals), grph_[vrtx].filteredSceneIndices_, *pSceneNormals_f);

    std::vector<Edge> new_edges;

    //--------------create-edges-between-views-by-Robot-Pose-----------------------------
    if(use_robot_pose_ && num_vertices(grph_)>1)
    {
        vertex_iter vertexIt, vertexEnd;
        for (boost::tie(vertexIt, vertexEnd) = vertices(grph_); vertexIt != vertexEnd; ++vertexIt)
        {
            Edge edge;
            if( grph_[*vertexIt].pScenePCl->header.frame_id.compare ( grph_[vrtx].pScenePCl->header.frame_id ) != 0 )
            {
                estimateViewTransformationByRobotPose ( *vertexIt, vrtx, grph_, edge );
                new_edges.push_back ( edge );
            }
        }
    }
    //----------END-create-edges-between-views-by-robot_pose-----------------------------------


    //----------calc-SIFT-features-create-edges-between-views-by-SIFT-----------------------------------
    if(scene_to_scene_)
    {
        pcl::ScopeTime ticp ("SIFT scene to scene registration...");
        calcSiftFeatures ( vrtx, grph_ );
        std::cout << "keypoints: " << grph_[vrtx].siftKeypointIndices_.indices.size() << std::endl;

        if (num_vertices(grph_)>1)
        {
            flann::Matrix<float> flann_data;
            flann::Index<DistT> *flann_index;
            multiview::convertToFLANN<pcl::Histogram<128> > ( grph_[vrtx].pSiftSignatures_, flann_data );
            flann_index = new flann::Index<DistT> ( flann_data, flann::KDTreeIndexParams ( 4 ) );
            flann_index->buildIndex ();

            //#pragma omp parallel for
            vertex_iter vertexIt, vertexEnd;
            for (boost::tie(vertexIt, vertexEnd) = vertices(grph_); vertexIt != vertexEnd; ++vertexIt)
            {
                Eigen::Matrix4f transformation;
                if( grph_[*vertexIt].pScenePCl->header.frame_id.compare ( grph_[vrtx].pScenePCl->header.frame_id ) != 0 )
                {
                    std::vector<Edge> edge;
                    estimateViewTransformationBySIFT ( *vertexIt, vrtx, grph_, flann_index, transformation, edge, use_gc_s2s_ );
                    for(size_t kk=0; kk < edge.size(); kk++)
                    {
                        new_edges.push_back (edge[kk]);
                    }
                }
            }
            delete flann_index;
        }
    }
    //----------END-create-edges-between-views-by-SIFT-----------------------------------


    //----------call-single-view-recognizer----------------------------------------------
    if(scene_to_scene_)
    {
        pSingleview_recognizer_->setISPK<flann::L1, pcl::Histogram<128> > (grph_[vrtx].pSiftSignatures_,
                                                                           grph_[vrtx].pScenePCl_f,
                                                                           grph_[vrtx].siftKeypointIndices_,
                                                                           faat_pcl::rec_3d_framework::SIFT);
    }
    pSingleview_recognizer_->setInputCloud(grph_[vrtx].pScenePCl_f, pSceneNormals_f);
    pSingleview_recognizer_->constructHypotheses();
    pSingleview_recognizer_->getSavedHypotheses(grph_[vrtx].hypotheses_);
    pSingleview_recognizer_->getKeypointsMultipipe(grph_[vrtx].pKeypointsMultipipe_);
    pSingleview_recognizer_->getKeypointIndices(grph_[vrtx].keypointIndices_);
    pcl::copyPointCloud(*pSceneNormals_f, grph_[vrtx].keypointIndices_, *(grph_[vrtx].pKeypointNormalsMultipipe_));
    assert(grph_[vrtx].pKeypointNormalsMultipipe_->points.size()
           == grph_[vrtx].pKeypointsMultipipe_->points.size());

    size_t sv_num_correspondences=0;
    std::map<std::string, faat_pcl::rec_3d_framework::ObjectHypothesis<PointT> >::const_iterator it_hyp;
    for(it_hyp = grph_[vrtx].hypotheses_.begin(); it_hyp !=grph_[vrtx].hypotheses_.end(); ++it_hyp)
    {
        sv_num_correspondences += it_hyp->second.correspondences_to_inputcloud->size();
    }

    pcl::StopWatch sv_overhead_pcl_time;

    std::vector < pcl::Correspondences > corresp_clusters_sv;

    {
        pcl::ScopeTime sv_hyp_construction ("Constructing hypotheses from feature matches...");
        pSingleview_recognizer_->constructHypothesesFromFeatureMatches(grph_[vrtx].hypotheses_,
                                                                       grph_[vrtx].pKeypointsMultipipe_,
                                                                       grph_[vrtx].pKeypointNormalsMultipipe_,
                                                                       grph_[vrtx].hypothesis_sv_,
                                                                       corresp_clusters_sv);
        sv_hyp_construction_time = sv_hyp_construction.getTime();
    }
    //    std::vector<float> fsv_mask_sv;
    //    pSingleview_recognizer_->preFilterWithFSV(grph_[vrtx].pScenePCl, fsv_mask_sv);
    //        pcl::visualization::PCLVisualizer::Ptr vis_fsv (new pcl::visualization::PCLVisualizer);
    //        for(size_t i=0; i<fsv_mask_sv.size(); i++)
    //        {
    //            vis_fsv->removeAllPointClouds();
    //            pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> handler_rgb_verified (grph_[vrtx].pScenePCl);
    //            vis_fsv->addPointCloud<pcl::PointXYZRGB> (grph_[vrtx].pScenePCl, handler_rgb_verified, "scene_cloud");
    //            std::cout << "fsv " << i << ": " << fsv_mask_sv[i] << std::endl;
    //            std::stringstream model_id;
    //            model_id << "cloud " << i;
    //            ConstPointInTPtr model_cloud = grph_[vrtx].hypothesis_sv_[i].model_->getAssembled (0.005f);
    //            typename pcl::PointCloud<PointT>::Ptr model_aligned (new pcl::PointCloud<PointT>);
    //            pcl::transformPointCloud (*model_cloud, *model_aligned, grph_[vrtx].hypothesis_sv_[i].transform_);
    //            pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> handler_rgb_verified2 (model_aligned);
    //            vis_fsv->addPointCloud<pcl::PointXYZRGB> (model_aligned, handler_rgb_verified2, model_id.str());
    //            vis_fsv->spin();
    //        }

    pSingleview_recognizer_->poseRefinement();
    std::vector<bool> mask_hv_sv;
    {
        pcl::ScopeTime ticp ("Hypotheses verification...");
        pSingleview_recognizer_->hypothesesVerification(mask_hv_sv);
        pSingleview_recognizer_->getVerifiedPlanes(grph_[vrtx].verified_planes_);
    }
    for (size_t j = 0; j < grph_[vrtx].hypothesis_sv_.size(); j++)
    {
        grph_[vrtx].hypothesis_sv_[j].verified_ = mask_hv_sv[j];
    }

    sv_overhead_time = sv_overhead_pcl_time.getTime();
    sv_total_time = total_pcl_time.getTime();
    //----------END-call-single-view-recognizer------------------------------------------

    //---copy-vertices-to-graph_final----------------------------
    Vertex vrtx_final = boost::add_vertex ( grph_final_ );
    grph_final_[vrtx_final] = grph_[vrtx]; // shallow copy is okay here

    if(new_edges.size())
    {
        Edge best_edge;
        best_edge = new_edges[0];

        if(new_edges.size()>1)  // take the "best" edge for transformation between the views
        {
            pcl::ScopeTime ticp ("Calculating edge weights...");
            calcEdgeWeight (grph_, new_edges);

            //------find best edge from the freshly inserted view and add it to the final graph-----------------

            for ( size_t i = 1; i < new_edges.size(); i++ )
            {
                //                bgvis_.visualizeEdge(new_edges[i], grph_);

                if ( grph_[new_edges[i]].edge_weight < grph_[best_edge].edge_weight )
                {
                    best_edge = new_edges[i];
                }
            }
        }
        //        bgvis_.visualizeEdge(new_edges[0], grph_);
        Vertex vrtx_src, vrtx_trgt;
        vrtx_src = source ( best_edge, grph_ );
        vrtx_trgt = target ( best_edge, grph_ );

        Edge e_cpy; bool b;
        tie ( e_cpy, b ) = add_edge ( vrtx_src, vrtx_trgt, grph_final_ );
        grph_final_[e_cpy] = grph_[best_edge]; // shallow copy is okay here
    }
    else
    {
        std::cout << "No edge for this vertex." << std::endl;
    }

    //---------Extend-hypotheses-from-other-view(s)------------------------------------------

    if (extension_mode_ == 0)
    {
        accumulatedHypotheses_.clear();

        pcl::StopWatch mv_feat_ext_pcl_time;
        extendFeatureMatchesRecursive(grph_final_, vrtx_final, accumulatedHypotheses_, pAccumulatedKeypoints_, pAccumulatedKeypointNormals_);
        resetHopStatus(grph_final_);

        for(it_hyp = accumulatedHypotheses_.begin(); it_hyp != accumulatedHypotheses_.end(); ++it_hyp)
        {
            total_num_correspondences += it_hyp->second.correspondences_to_inputcloud->size();
        }

        mv_feat_ext_time = mv_feat_ext_pcl_time.getTime();

        std::vector < pcl::Correspondences > corresp_clusters_mv;

        pcl::StopWatch mv_hyp_construct;
        pSingleview_recognizer_->constructHypothesesFromFeatureMatches(accumulatedHypotheses_,
                                                                       pAccumulatedKeypoints_,
                                                                       pAccumulatedKeypointNormals_,
                                                                       grph_final_[vrtx_final].hypothesis_mv_,
                                                                       corresp_clusters_mv);
        mv_hyp_construct_time = mv_hyp_construct.getTime();

        {
            pcl::ScopeTime ticp("Multi-view ICP...");
            pSingleview_recognizer_->poseRefinement();
            mv_icp_time = ticp.getTime();
        }
        std::vector<bool> mask_hv_mv;

        pcl::StopWatch mv_hyp_ver_pcl_time;
        pSingleview_recognizer_->hypothesesVerification(mask_hv_mv);
        mv_hyp_ver_time = mv_hyp_ver_pcl_time.getTime();

        pcl::StopWatch augment_verified_hyp_pcl_time;
        for(size_t i=0; i<mask_hv_mv.size(); i++)
        {
            grph_final_[vrtx_final].hypothesis_mv_[i].verified_ = static_cast<int>(mask_hv_mv[i]);

            if(mask_hv_mv[i])
            {
                const std::string id = grph_final_[vrtx_final].hypothesis_mv_[i].model_->id_;
                std::map<std::string, faat_pcl::rec_3d_framework::ObjectHypothesis<PointT> >::iterator it_hyp_sv;
                it_hyp_sv = grph_final_[vrtx_final].hypotheses_.find(id);
                if (it_hyp_sv == grph_final_[vrtx_final].hypotheses_.end())
                {
                    PCL_ERROR("There has not been a single keypoint detected for model %s", id.c_str());
                }
                else
                {
                    for(size_t jj = 0; jj < corresp_clusters_mv[i].size(); jj++)
                    {
                        const pcl::Correspondence c = corresp_clusters_mv[i][jj];
                        const int kp_scene_idx = c.index_match;
                        const int kp_model_idx = c.index_query;
                        const PointT keypoint_model = accumulatedHypotheses_[id].correspondences_pointcloud->points[kp_model_idx];
                        const pcl::Normal keypoint_normal_model = accumulatedHypotheses_[id].normals_pointcloud->points[kp_model_idx];
                        const PointT keypoint_scene = pAccumulatedKeypoints_->points[kp_scene_idx];
                        const pcl::Normal keypoint_normal_scene = pAccumulatedKeypointNormals_->points[kp_scene_idx];
                        const int index_to_flann_models = accumulatedHypotheses_[id].indices_to_flann_models_[kp_model_idx];

                        // only add correspondences which correspondences to both, model and scene keypoint,
                        // are not already saved in the hypotheses coming from single view recognition only.
                        // As the keypoints from single view rec. are pushed in the front, we only have to check
                        // if the indices of the new correspondences are outside of these keypoint sizes.
                        // Also, we don't have to check if these new keypoints are redundant because this
                        // is already done in the function "extendFeatureMatchesRecursive(..)".

                        if(kp_model_idx >= it_hyp_sv->second.correspondences_pointcloud->points.size()
                                && kp_scene_idx >= grph_final_[vrtx_final].pKeypointsMultipipe_->points.size())
                        {
                            pcl::Correspondence c_new = c;  // to keep hypothesis' class union member distance
                            c_new.index_match = grph_final_[vrtx_final].pKeypointsMultipipe_->points.size();
                            c_new.index_query = it_hyp_sv->second.correspondences_pointcloud->points.size();
                            it_hyp_sv->second.correspondences_to_inputcloud->push_back(c_new);

                            it_hyp_sv->second.correspondences_pointcloud->points.push_back(keypoint_model);
                            it_hyp_sv->second.normals_pointcloud->points.push_back(keypoint_normal_model);
                            it_hyp_sv->second.indices_to_flann_models_.push_back(index_to_flann_models);
                            grph_final_[vrtx_final].pKeypointsMultipipe_->points.push_back(keypoint_scene);
                            grph_final_[vrtx_final].pKeypointNormalsMultipipe_->points.push_back(keypoint_normal_scene);
                        }
                    }
                }
            }
        }
        std::cout << "Augmentation of verified keypoint correspondences took "
                  << augment_verified_hyp_pcl_time.getTime() << "ms.";
    }
    else if(extension_mode_==1) // transform full hypotheses (not single keypoint correspondences)
    {
        bool use_unverified_single_view_hypotheses = true;

        // copy single-view hypotheses into multi-view hypotheses
        for(size_t hyp_sv_id=0; hyp_sv_id<grph_final_[vrtx_final].hypothesis_sv_.size(); hyp_sv_id++)
        {
            grph_final_[vrtx_final].hypothesis_mv_.push_back(grph_final_[vrtx_final].hypothesis_sv_[hyp_sv_id]);
        }

        extendHypothesisRecursive(grph_final_, vrtx_final, grph_final_[vrtx_final].hypothesis_mv_, use_unverified_single_view_hypotheses);
        resetHopStatus(grph_final_);

        std::vector<ModelTPtr> mv_models;
        std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f>> mv_transforms;
        for(size_t hyp_mv_id=0; hyp_mv_id<grph_final_[vrtx_final].hypothesis_mv_.size(); hyp_mv_id++)
        {
            mv_models.push_back(grph_final_[vrtx_final].hypothesis_mv_[hyp_mv_id].model_);
            mv_transforms.push_back(grph_final_[vrtx_final].hypothesis_mv_[hyp_mv_id].transform_);
        }
        pSingleview_recognizer_->setModelsAndTransforms(mv_models, mv_transforms);
        pSingleview_recognizer_->poseRefinement();

        std::vector<bool> mask_hv_mv;
        {
            pcl::ScopeTime ticp ("Hypotheses verification...");
            pSingleview_recognizer_->hypothesesVerification(mask_hv_mv);
            pSingleview_recognizer_->getVerifiedPlanes(grph_final_[vrtx_final].verified_planes_);
        }
        for (size_t j = 0; j < grph_final_[vrtx_final].hypothesis_mv_.size(); j++)
        {
            grph_final_[vrtx_final].hypothesis_mv_[j].verified_ = mask_hv_mv[j];
        }

        if(go3d_)
        {
            double max_keypoint_dist_mv_ = 2.5f;

            bool go3d_add_planes = false;
            bool go3d_icp_ = true;
            bool go3d_icp_model_to_scene_ = false;

            //Noise model parameters
            double max_angle = 70.f;
            double lateral_sigma = 0.0015f;
            double nm_integration_min_weight_ = 0.25f;
            bool depth_edges = true;

            bool visualize_output_go_3D = true;

//            pcl::PointCloud<pcl::PointXYZRGB>::Ptr big_cloud (new pcl::PointCloud<pcl::PointXYZRGB>);
//            createBigPointCloudRecursive (grph_final_, vrtx_final, big_cloud);
//            resetHopStatus(grph_final_);

            faat_pcl::utils::noise_models::NguyenNoiseModel<pcl::PointXYZRGB> nm;
            nm.setInputCloud(grph_final_[vrtx_final].pScenePCl);
            nm.setInputNormals(grph_final_[vrtx_final].pSceneNormals);
            nm.setLateralSigma(lateral_sigma);
            nm.setMaxAngle(max_angle);
            nm.setUseDepthEdges(depth_edges);
            nm.compute();
            nm.getWeights(grph_final_[vrtx_final].nguyens_noise_model_weights_);

            pcl::PointCloud<pcl::PointXYZRGB>::Ptr foo_filtered;
            std::vector<int> kept_indices;
            nm.getFilteredCloudRemovingPoints(foo_filtered, 0.8f, kept_indices);

            // finally filter by distance and store kept indices in vertex
            grph_final_[vrtx_final].nguyens_kept_indices_.resize(kept_indices.size());
            size_t kept=0;

            for(size_t i=0; i < kept_indices.size(); i++)
            {
                float dist = grph_final_[vrtx_final].pScenePCl->points[kept_indices[i]].getVector3fMap().norm();
                if(dist < max_keypoint_dist_mv_)
                {
                    grph_final_[vrtx_final].nguyens_kept_indices_[kept] = kept_indices[i];
                    kept++;
                }
            }
            grph_final_[vrtx_final].nguyens_kept_indices_.resize(kept);

            std::cout << "kept:" << kept << " for a max point distance of " << max_keypoint_dist_mv_ << std::endl;

            std::pair<vertex_iter, vertex_iter> vp;

            std::vector< std::vector<float> > views_noise_weights;
            std::vector<pcl::PointCloud<pcl::PointXYZRGB>::Ptr> original_clouds;
            std::vector<pcl::PointCloud<pcl::Normal>::Ptr> normal_clouds;
            std::vector<pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr> occlusion_clouds;

            //visualize the model hypotheses
            std::vector<pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr> aligned_models;
            std::vector < std::string > ids;
            std::vector < Eigen::Matrix4f > transforms_to_global;
            std::vector<typename pcl::PointCloud<pcl::Normal>::ConstPtr> aligned_normals;

            for (vp = vertices (grph_final_); vp.first != vp.second; ++vp.first)
            {
                views_noise_weights.push_back(grph_final_[*vp.first].nguyens_noise_model_weights_); // TODO: Does the vertices order matter?

                pcl::PointCloud<pcl::Normal>::Ptr normal_cloud_filtered (new pcl::PointCloud<pcl::Normal>());
                pcl::PointCloud<pcl::Normal>::Ptr normal_cloud_filtered_trans (new pcl::PointCloud<pcl::Normal>());
                pcl::copyPointCloud(*(grph_final_[vrtx_final].pSceneNormals), grph_final_[vrtx_final].nguyens_kept_indices_, *normal_cloud_filtered);
                faat_pcl::utils::miscellaneous::transformNormals(normal_cloud_filtered, normal_cloud_filtered_trans, grph_final_[*vp.first].absolute_pose_);

                pcl::PointCloud<pcl::PointXYZRGB>::Ptr scene_cloud_filtered (new pcl::PointCloud<pcl::PointXYZRGB>(*grph_final_[*vp.first].pScenePCl));
                pcl::PointCloud<pcl::PointXYZRGB>::Ptr scene_cloud_filtered_trans (new pcl::PointCloud<pcl::PointXYZRGB>(*grph_final_[*vp.first].pScenePCl));
                pcl::copyPointCloud(*(grph_final_[vrtx_final].pScenePCl), grph_final_[vrtx_final].nguyens_kept_indices_, *scene_cloud_filtered);
                pcl::transformPointCloud(*scene_cloud_filtered, *scene_cloud_filtered_trans,  grph_final_[*vp.first].absolute_pose_);

                original_clouds.push_back(grph_final_[*vp.first].pScenePCl);
                normal_clouds.push_back(grph_final_[*vp.first].pSceneNormals);
                transforms_to_global.push_back (grph_final_[*vp.first].absolute_pose_);
            }

            for(size_t hyp_id=0; hyp_id < grph_final_[vrtx_final].hypothesis_mv_.size(); hyp_id++)
            {
                ModelTPtr model = grph_final_[vrtx_final].hypothesis_mv_[hyp_id].model_;
                ConstPointInTPtr model_cloud = model->getAssembled (pSingleview_recognizer_->hv_params_.resolution_);
                pcl::PointCloud<pcl::Normal>::ConstPtr normal_cloud = model->getNormalsAssembled (pSingleview_recognizer_->hv_params_.resolution_);

                typename pcl::PointCloud<pcl::Normal>::Ptr normal_aligned (new pcl::PointCloud<pcl::Normal>(*normal_cloud));
                typename pcl::PointCloud<PointT>::Ptr model_aligned (new pcl::PointCloud<PointT>(*model_cloud));

                aligned_models.push_back (model_aligned);
                aligned_normals.push_back(normal_aligned);
                ids.push_back (grph_final_[vrtx_final].hypothesis_mv_[hyp_id].model_->id_);
            }

            std::cout << "number of hypotheses for GO3D:" << grph_final_[vrtx_final].hypothesis_mv_.size() << std::endl;
            if(grph_final_[vrtx_final].hypothesis_mv_.size() > 0)
            {
                pcl::PointCloud<pcl::PointXYZRGB>::Ptr big_cloud_go3D(new pcl::PointCloud<pcl::PointXYZRGB>);
                pcl::PointCloud<pcl::Normal>::Ptr big_cloud_go3D_normals(new pcl::PointCloud<pcl::Normal>);

                //obtain big cloud and occlusion clouds based on new noise model integration
                pcl::PointCloud<pcl::PointXYZRGB>::Ptr octree_cloud(new pcl::PointCloud<pcl::PointXYZRGB>);
                faat_pcl::utils::NMBasedCloudIntegration<pcl::PointXYZRGB> nmIntegration;
                nmIntegration.setInputClouds(original_clouds);
                nmIntegration.setResolution(pSingleview_recognizer_->hv_params_.resolution_);
                nmIntegration.setWeights(views_noise_weights);
                nmIntegration.setTransformations(transforms_to_global);
                nmIntegration.setMinWeight(nm_integration_min_weight_);
                nmIntegration.setInputNormals(normal_clouds);
                nmIntegration.setMinPointsPerVoxel(1);
                nmIntegration.setFinalResolution(pSingleview_recognizer_->hv_params_.resolution_);
                nmIntegration.compute(octree_cloud);

                std::vector<pcl::PointCloud<pcl::PointXYZRGB>::Ptr> used_clouds;
                pcl::PointCloud<pcl::Normal>::Ptr big_normals(new pcl::PointCloud<pcl::Normal>);
                nmIntegration.getOutputNormals(big_normals);
                nmIntegration.getInputCloudsUsed(used_clouds);

                occlusion_clouds.resize(used_clouds.size());
                for(size_t kk=0; kk < used_clouds.size(); kk++)
                {
                    occlusion_clouds[kk].reset(new pcl::PointCloud<pcl::PointXYZRGB>(*used_clouds[kk]));
                }

                big_cloud_go3D = octree_cloud;
                big_cloud_go3D_normals = big_normals;

                //Refine aligned models with ICP
                if(go3d_icp_)
                {
                    pcl::ScopeTime t("GO3D ICP...\n");
                    float icp_max_correspondence_distance_ = 0.01f;

#pragma omp parallel for num_threads(4) schedule(dynamic)
                    for(size_t kk=0; kk < grph_final_[vrtx_final].hypothesis_mv_.size(); kk++)
                    {
                        if(!grph_final_[vrtx_final].hypothesis_mv_[kk].extended_)
                        {
                            continue;
                        }

                        ModelTPtr model = grph_final_[vrtx_final].hypothesis_mv_[kk].model_;

                        //cut scene based on model cloud
                        boost::shared_ptr < distance_field::PropagationDistanceField<pcl::PointXYZRGB> > dt;
                        model->getVGDT (dt);

                        pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr cloud;
                        dt->getInputCloud (cloud);

                        Eigen::Matrix4f scene_to_model_trans = grph_final_[vrtx_final].hypothesis_mv_[kk].transform_.inverse ();
                        pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_voxelized_icp_transformed (new pcl::PointCloud<pcl::PointXYZRGB> ());
                        pcl::transformPointCloud (*big_cloud_go3D, *cloud_voxelized_icp_transformed, scene_to_model_trans);

                        float thres = icp_max_correspondence_distance_ * 2.f;
                        pcl::PointXYZRGB minPoint, maxPoint;
                        pcl::getMinMax3D(*cloud, minPoint, maxPoint);
                        minPoint.x -= thres;
                        minPoint.y -= thres;
                        minPoint.z -= thres;

                        maxPoint.x += thres;
                        maxPoint.y += thres;
                        maxPoint.z += thres;

                        pcl::CropBox<pcl::PointXYZRGB> cropFilter;
                        cropFilter.setInputCloud (cloud_voxelized_icp_transformed);
                        cropFilter.setMin(minPoint.getVector4fMap());
                        cropFilter.setMax(maxPoint.getVector4fMap());

                        pcl::PointCloud<pcl::PointXYZRGB>::Ptr cloud_voxelized_icp_cropped (new pcl::PointCloud<pcl::PointXYZRGB> ());
                        cropFilter.filter (*cloud_voxelized_icp_cropped);

                        if(go3d_icp_model_to_scene_)
                        {
                            Eigen::Matrix4f s2m = scene_to_model_trans.inverse();
                            pcl::transformPointCloud (*cloud_voxelized_icp_cropped, *cloud_voxelized_icp_cropped, s2m);

                            pcl::IterativeClosestPoint<pcl::PointXYZRGB, pcl::PointXYZRGB> icp;
                            icp.setInputTarget (cloud_voxelized_icp_cropped);
                            icp.setInputSource(aligned_models[kk]);
                            icp.setMaxCorrespondenceDistance(icp_max_correspondence_distance_);
                            icp.setMaximumIterations(pSingleview_recognizer_->get_icp_iterations());
                            icp.setRANSACIterations(5000);
                            icp.setEuclideanFitnessEpsilon(1e-12);
                            icp.setTransformationEpsilon(1e-12);
                            pcl::PointCloud < PointT >::Ptr model_aligned( new pcl::PointCloud<PointT> );
                            icp.align (*model_aligned, grph_final_[vrtx_final].hypothesis_mv_[kk].transform_);

                            grph_final_[vrtx_final].hypothesis_mv_[kk].transform_ = icp.getFinalTransformation();
                        }
                        else
                        {
                            faat_pcl::rec_3d_framework::VoxelBasedCorrespondenceEstimation<pcl::PointXYZRGB, pcl::PointXYZRGB>::Ptr
                                    est (
                                        new faat_pcl::rec_3d_framework::VoxelBasedCorrespondenceEstimation<
                                        pcl::PointXYZRGB,
                                        pcl::PointXYZRGB> ());

                            pcl::registration::CorrespondenceRejectorSampleConsensus<pcl::PointXYZRGB>::Ptr
                                    rej (
                                        new pcl::registration::CorrespondenceRejectorSampleConsensus<
                                        pcl::PointXYZRGB> ());

                            est->setVoxelRepresentationTarget (dt);
                            est->setInputSource (cloud_voxelized_icp_cropped);
                            est->setInputTarget (cloud);
                            est->setMaxCorrespondenceDistance (icp_max_correspondence_distance_);
                            est->setMaxColorDistance (-1, -1);

                            rej->setInputTarget (cloud);
                            rej->setMaximumIterations (5000);
                            rej->setInlierThreshold (icp_max_correspondence_distance_);
                            rej->setInputSource (cloud_voxelized_icp_cropped);

                            pcl::IterativeClosestPoint<pcl::PointXYZRGB, pcl::PointXYZRGB> reg;
                            reg.setCorrespondenceEstimation (est);
                            reg.addCorrespondenceRejector (rej);
                            reg.setInputTarget (cloud); //model
                            reg.setInputSource (cloud_voxelized_icp_cropped); //scene
                            reg.setMaximumIterations (pSingleview_recognizer_->get_icp_iterations());
                            reg.setEuclideanFitnessEpsilon (1e-12);
                            reg.setTransformationEpsilon (0.0001f * 0.0001f);

                            pcl::registration::DefaultConvergenceCriteria<float>::Ptr convergence_criteria;
                            convergence_criteria = reg.getConvergeCriteria ();
                            convergence_criteria->setAbsoluteMSE (1e-12);
                            convergence_criteria->setMaximumIterationsSimilarTransforms (15);
                            convergence_criteria->setFailureAfterMaximumIterations (false);

                            PointInTPtr output (new pcl::PointCloud<pcl::PointXYZRGB> ());
                            reg.align (*output);
                            Eigen::Matrix4f trans, icp_trans;
                            trans = reg.getFinalTransformation () * scene_to_model_trans;
                            icp_trans = trans.inverse ();

                            grph_final_[vrtx_final].hypothesis_mv_[kk].transform_ = icp_trans;
                        }
                    }
                }

                //transform models to be used during GO3D
#pragma omp parallel for num_threads(4) schedule(dynamic)
                for(size_t kk=0; kk < grph_final_[vrtx_final].hypothesis_mv_.size(); kk++)
                {
                    Eigen::Matrix4f trans = grph_final_[vrtx_final].hypothesis_mv_[kk].transform_;
                    ModelTPtr model = grph_final_[vrtx_final].hypothesis_mv_[kk].model_;

                    typename pcl::PointCloud<PointT>::Ptr model_aligned ( new pcl::PointCloud<PointT> );
                    ConstPointInTPtr model_cloud = model->getAssembled (pSingleview_recognizer_->hv_params_.resolution_);
                    pcl::transformPointCloud (*model_cloud, *model_aligned, trans);
                    aligned_models[kk] = model_aligned;

                    pcl::PointCloud<pcl::Normal>::ConstPtr normal_cloud = model->getNormalsAssembled (pSingleview_recognizer_->hv_params_.resolution_);
                    typename pcl::PointCloud<pcl::Normal>::Ptr normal_aligned (new pcl::PointCloud<pcl::Normal>);
                    faat_pcl::utils::miscellaneous::transformNormals(normal_cloud, normal_aligned, trans);
                    aligned_normals[kk] = normal_aligned;
                }

                //Instantiate HV go 3D, reimplement addModels that will reason about occlusions
                //Set occlusion cloudS!!
                //Set the absolute poses so we can go from the global coordinate system to the occlusion clouds
                //TODO: Normals might be a problem!! We need normals from the models and normals from the scene, correctly oriented!
                //right now, all normals from the scene will be oriented towards some weird 0, same for models actually

                /*eps_angle_threshold_ = 0.25;
                min_points_ = 20;
                curvature_threshold_ = 0.04f;
                cluster_tolerance_ = 0.015f;
                setSmoothSegParameters (float t_eps, float curv_t, float dist_t, int min_points = 20)*/

                std::cout << "GO 3D parameters:" << std::endl
                          << "go3d_inlier_threshold_:" << pSingleview_recognizer_->hv_params_.inlier_threshold_ << std::endl
                          << "go3d_clutter_radius_:" << pSingleview_recognizer_->hv_params_.radius_clutter_ << std::endl
                          << "go3d_outlier_regularizer:" << pSingleview_recognizer_->hv_params_.regularizer_ << std::endl
                          << "go3d_clutter_regularizer:" << pSingleview_recognizer_->hv_params_.clutter_regularizer_ << std::endl
                          << "go3d_use_supervoxels:" << pSingleview_recognizer_->hv_params_.use_supervoxels_ << std::endl
                          << "go3d_color_sigma L:" << pSingleview_recognizer_->hv_params_.color_sigma_l_ << std::endl
                          << "go3d_color_sigma AB:" << pSingleview_recognizer_->hv_params_.color_sigma_ab_ << std::endl
                          << "go3d_detect_clutter:" << pSingleview_recognizer_->hv_params_.detect_clutter_ << std::endl
                          << "go3d_ignore_color:" << pSingleview_recognizer_->hv_params_.ignore_color_ << std::endl;

                faat_pcl::GO3D<PointT, PointT> go3d;
                go3d.setResolution (pSingleview_recognizer_->hv_params_.resolution_);
                go3d.setInlierThreshold (pSingleview_recognizer_->hv_params_.inlier_threshold_);
                go3d.setRadiusClutter (pSingleview_recognizer_->hv_params_.radius_clutter_);
                go3d.setRegularizer (pSingleview_recognizer_->hv_params_.regularizer_);
                go3d.setClutterRegularizer (pSingleview_recognizer_->hv_params_.clutter_regularizer_);
                go3d.setDetectClutter (pSingleview_recognizer_->hv_params_.detect_clutter_);
                go3d.setOcclusionThreshold (pSingleview_recognizer_->hv_params_.occlusion_threshold_);
                go3d.setOptimizerType (pSingleview_recognizer_->hv_params_.optimizer_type_);
                go3d.setUseReplaceMoves(pSingleview_recognizer_->hv_params_.use_replace_moves_);
                go3d.setRadiusNormals(pSingleview_recognizer_->hv_params_.radius_normals_);
                go3d.setRequiresNormals(pSingleview_recognizer_->hv_params_.requires_normals_);
                go3d.setInitialStatus(pSingleview_recognizer_->hv_params_.initial_status_);
                go3d.setIgnoreColor (pSingleview_recognizer_->hv_params_.ignore_color_);
                go3d.setColorSigma (pSingleview_recognizer_->hv_params_.color_sigma_l_,
                                  pSingleview_recognizer_->hv_params_.color_sigma_ab_);
                go3d.setHistogramSpecification(pSingleview_recognizer_->hv_params_.histogram_specification_);
                go3d.setSmoothSegParameters(pSingleview_recognizer_->hv_params_.smooth_seg_params_eps_,
                                            pSingleview_recognizer_->hv_params_.smooth_seg_params_curv_t_,
                                            pSingleview_recognizer_->hv_params_.smooth_seg_params_dist_t_,
                                            pSingleview_recognizer_->hv_params_.smooth_seg_params_min_points_);
                go3d.setVisualizeGoCues(0);
                go3d.setUseSuperVoxels(pSingleview_recognizer_->hv_params_.use_supervoxels_);
                go3d.setZBufferSelfOcclusionResolution (pSingleview_recognizer_->hv_params_.z_buffer_self_occlusion_resolution_);
                go3d.setOcclusionsClouds (occlusion_clouds);
                go3d.setHypPenalty (pSingleview_recognizer_->hv_params_.hyp_penalty_);
                go3d.setDuplicityCMWeight(pSingleview_recognizer_->hv_params_.duplicity_cm_weight_);
                go3d.setSceneCloud (big_cloud_go3D);

                go3d.setSceneAndNormals(big_cloud_go3D, big_cloud_go3D_normals);
                go3d.setAbsolutePoses (transforms_to_global);
                go3d.addNormalsClouds(aligned_normals);
                go3d.addModels (aligned_models, true);

                std::vector<faat_pcl::PlaneModel<PointT> > planes_found;

                /*if(go3d_add_planes)
                {
                    faat_pcl::MultiPlaneSegmentation<PointT> mps;
                    mps.setInputCloud(big_cloud_after_mv);
                    mps.setMinPlaneInliers(5000);
                    mps.setResolution(leaf);
                    //mps.setNormals(grph[vrtx].normals_);
                    mps.setMergePlanes(false);
                    mps.segment();
                    planes_found = mps.getModels();

                    go.addPlanarModels(planes_found);
                    for(size_t kk=0; kk < planes_found.size(); kk++)
                    {
                        std::stringstream plane_id;
                        plane_id << "plane_" << kk;
                        ids.push_back(plane_id.str());
                    }
                }*/

                go3d.setObjectIds (ids);

                go3d.verify ();
                std::vector<bool> mask;
                go3d.getMask (mask);

                for(size_t kk=0; kk < grph_final_[vrtx_final].hypothesis_mv_.size(); kk++)
                {
                    grph_final_[vrtx_final].hypothesis_mv_[kk].verified_ = mask[kk];
                }

                if(visualize_output_go_3D && visualize_output_)
                {
                    if(!go3d_vis_)
                    {
                        go3d_vis_.reset(new pcl::visualization::PCLVisualizer("GO 3D visualization"));
                        for(size_t vp_id=1; vp_id<=6; vp_id++)
                        {
                            go_3d_viewports_.push_back(vp_id);
                        }
                        go3d_vis_->createViewPort (0, 0, 0.5, 0.33, go_3d_viewports_[0]);
                        go3d_vis_->createViewPort (0.5, 0, 1, 0.33, go_3d_viewports_[1]);
                        go3d_vis_->createViewPort (0, 0.33, 0.5, 0.66, go_3d_viewports_[2]);
                        go3d_vis_->createViewPort (0.5, 0.33, 1, 0.66, go_3d_viewports_[3]);
                        go3d_vis_->createViewPort (0, 0.66, 0.5, 1, go_3d_viewports_[4]);
                        go3d_vis_->createViewPort (0.5, 0.66, 1, 1, go_3d_viewports_[5]);
                    }

                    for(size_t vp_id=0; vp_id<6; vp_id++)
                    {
                        go3d_vis_->removeAllPointClouds(go_3d_viewports_[vp_id]);
                    }

                    pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> handler (big_cloud_go3D);
                    go3d_vis_->addPointCloud (big_cloud_go3D, handler, "big", go_3d_viewports_[0]);

                    /*pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> handler (big_cloud_vx_after_mv);
            vis.addPointCloud (big_cloud_vx_after_mv, handler, "big", v1);*/

                    for(size_t i=0; i < grph_final_[vrtx_final].hypothesis_mv_.size(); i++)
                    {
                        pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> handler_rgb_verified (aligned_models[i]);
                        std::stringstream name;
                        name << "Hypothesis_model_" << i;
                        go3d_vis_->addPointCloud<pcl::PointXYZRGB> (aligned_models[i], handler_rgb_verified, name.str (), go_3d_viewports_[1]);
                    }

                    pcl::PointCloud<pcl::PointXYZRGBA>::Ptr smooth_cloud_ =  go3d.getSmoothClustersRGBCloud();
                    if(smooth_cloud_)
                    {
                        pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGBA> random_handler (smooth_cloud_);
                        go3d_vis_->addPointCloud<pcl::PointXYZRGBA> (smooth_cloud_, random_handler, "smooth_cloud", go_3d_viewports_[4]);
                    }

                    for (size_t i = 0; i < grph_final_[vrtx_final].hypothesis_mv_.size (); i++)
                    {
                        if (mask[i])
                        {
                            std::cout << "Verified:" << ids[i] << std::endl;
                            pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> handler_rgb_verified (aligned_models[i]);
                            std::stringstream name;
                            name << "verified" << i;
                            go3d_vis_->addPointCloud<pcl::PointXYZRGB> (aligned_models[i], handler_rgb_verified, name.str (), go_3d_viewports_[2]);

                            pcl::PointCloud<pcl::PointXYZRGB>::Ptr inliers_outlier_cloud;
                            go3d.getInlierOutliersCloud((int)i, inliers_outlier_cloud);

                            {
                                pcl::visualization::PointCloudColorHandlerRGBField<pcl::PointXYZRGB> handler_rgb_verified (inliers_outlier_cloud);
                                std::stringstream name;
                                name << "verified_visible_" << i;
                                go3d_vis_->addPointCloud<pcl::PointXYZRGB> (inliers_outlier_cloud, handler_rgb_verified, name.str (), go_3d_viewports_[3]);
                            }
                        }
                    }

                    if(go3d_add_planes)
                    {
                        for(size_t i=0; i < planes_found.size(); i++)
                        {
                            if(!mask[i + aligned_models.size()])
                                continue;

                            std::stringstream pname;
                            pname << "plane_" << i;

                            pcl::visualization::PointCloudColorHandlerRandom<PointT> scene_handler(planes_found[i].plane_cloud_);
                            go3d_vis_->addPointCloud<PointT> (planes_found[i].plane_cloud_, scene_handler, pname.str(), go_3d_viewports_[2]);

                            //pname << "chull";
                            //vis.addPolygonMesh (planes_found[i].convex_hull_, pname.str(), v3);
                        }
                    }
                    go3d_vis_->setBackgroundColor(1,1,1);
                    go3d_vis_->spin ();
                }
            }
        }
    }

    if(visualize_output_)
        bgvis_.visualizeGraph(grph_final_, vis_);

    resetHopStatus(grph_final_);

    grph_[vrtx] = grph_final_[vrtx_final]; // shallow copy is okay here

    //-------Clean-up-graph-------------------
    total_time = total_pcl_time.getTime();

    outputgraph ( grph_, "complete_graph.dot" );
    outputgraph ( grph_final_, "Final_with_Hypothesis_extension.dot" );

    times_.push_back(total_time);
    times_.push_back(sv_hyp_construction_time);
    times_.push_back(sv_overhead_time);
    times_.push_back(mv_hyp_construct_time);
    times_.push_back(mv_hyp_ver_time);
    times_.push_back(mv_feat_ext_time);
    times_.push_back(mv_icp_time);
    times_.push_back(total_num_correspondences);
    times_.push_back(sv_num_correspondences);

    if(extension_mode_==0 )
    {
        // bgvis_.visualizeWorkflow(vrtx_final, grph_final_, pAccumulatedKeypoints_);
        //    bgvis_.createImage(vrtx_final, grph_final_, "/home/thomas/Desktop/test.jpg");
    }

    pruneGraph(grph_, max_vertices_in_graph_);
    pruneGraph(grph_final_, max_vertices_in_graph_);

    outputgraph ( grph_final_, "final_after_deleting_old_vertex.dot" );
    outputgraph ( grph_, "grph_after_deleting_old_vertex.dot" );
    return true;
}
