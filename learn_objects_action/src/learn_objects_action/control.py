import rospy
import smach
import actionlib

from geometry_msgs.msg import Pose, PoseStamped
from sensor_msgs.msg import PointCloud2
from std_msgs.msg import String
from util import get_ros_service
from  strands_navigation_msgs.msg import MonitoredNavigationAction,MonitoredNavigationGoal
from object_view_generator.srv import GetTrajectoryPoints
import yaml
from world_state.observation import MessageStoreObject, Observation, TransformationStore
from world_state.identification import ObjectIdentification
from world_state.state import World, Object
import os

class TravelAroundObject(smach.State):
    def __init__(self):
        smach.State.__init__( self, outcomes=['error', 'done','preempted'],
                              input_keys=['dynamic_object_centroid','action_goal','object'],
                              output_keys=['object'] )
        ## Camera tracking services
        #start_camera_tracker = get_ros_service("/camera_tracker/start_recording", start_tracker)
        #stop_camera_tracker = get_ros_service("/camera_tracker/stop_recording", stop_tracker)
        #visualize_tracker = get_ros_service("/camera_tracker/vis_compound", visualize_compound)
        #tracking_results = get_ros_service("/camera_tracker/get_results", get_tracking_results)
        ## Object learning
        #learn_object_model = get_ros_service("/dynamic_object_learning/learn_object", learn_object)
        #save_object_model = get_ros_service("/dynamic_object_learning/save_model", save_model)

        ## Recognition services
        #get_rec_configuration = get_ros_service(
        #        "/recognition_service/get_configuration", get_configuration)
        #recognize_object = get_ros_service(
        #        "/recognition_service/mp_recognition", recognize)

        self._get_plan_points = get_ros_service('/test_nav_goal', GetTrajectoryPoints)
        self._nav_client = actionlib.SimpleActionClient('/monitored_navigation', MonitoredNavigationAction)
        rospy.loginfo("Waiting for monitored_nav...")
        if not self._nav_client.wait_for_server(rospy.Duration(5)):
            rospy.logerr("Monitored navigation action can't be found!")
            raise Exception("LearnObject SM can't initialise; missing service.")
        rospy.loginfo("Got it.")
        
        self._image_publisher =  rospy.Publisher("/object_learning/object_view",
                                                 PointCloud2)
        self._status_publisher =  rospy.Publisher("/object_learning/status",
                                                  String)


    def execute(self, userdata):
        self._status_publisher.publish(String("start_viewing"))
        # Load the waypoint to soma from file, ugly TODO: properly
        with open(os.path.expanduser("~/.waypointsomas"), "r") as f:
            somas = yaml.load(f.read())	
        soma_region = somas[userdata.action_goal.waypoint]

        try:
            # stop transformation
            p =  Pose()
            p.position.x = userdata.dynamic_object_centroid.x
            p.position.y = userdata.dynamic_object_centroid.y

            poses = self._get_plan_points(min_dist=1.0, 
                                          max_dist=1.5,
                                          number_views=25,
                                          inflation_radius=0.3, 
                                          target_pose=p,
                                          SOMA_region=soma_region)
            if len(poses.goals.poses)<1:
                rospy.loginfo("This object has no observation points. That is a real shame.")
            for i,p in enumerate(poses.goals.poses):
                rospy.loginfo("Navigationg to trajectory point %d / %d..." %
                              (i+1,len(poses.goals.poses)))
                targ = PoseStamped()
                targ.header.frame_id="/map"
                targ.pose=p
                goal = MonitoredNavigationGoal("move_base", targ)
                self._nav_client.send_goal(goal)
                while not self._nav_client.wait_for_result(rospy.Duration(0.5)):
                    if self.preempt_requested():
                        rospy.logwarn("FFS I've ben pre-empted while executing navigation.")
                        self.service_preempt()
                        self._nav_client.cancel_goal()
                        self._nav_client.wait_for_result()
                        self._status_publisher.publish(String("stop_viewing"))
                        return "preempted"

                        
                result = self._nav_client.get_result()
                if result.outcome !=  "succeeded" and result.recovered != True:
                    rospy.logwarn("Object learning failed to do navigation "
                                  "crap. Monitored navigation reported: %s" %
                                  str(result))
                    return "error"
                if result.outcome != "succeeded":
                    # got to retry this one since it was recovered?
                    # we will only do it once...
                    self._nav_client.send_goal(goal)
                    self._nav_client.wait_for_result()
                    result = self._nav_client.get_result()
                    if result.outcome != "succeeded":
                        rospy.logwarn("Object learning failed do to navigation "
                                      "-> second shot of monitored "
                                      "navigation reported: %s" %
                                      str(result))
                        return "error"
                rospy.loginfo("Capturing a still image for the object_manager...")
                try:
                    rospy.sleep(5) # ugly wait for PTU tracking to stabalize
                    image = rospy.wait_for_message("/head_xtion/depth_registered/points",
                                           PointCloud2,
                                           timeout=10.0)
                    self._image_publisher.publish(image)

                    observation = Observation.make_observation()
                    userdata['object'].add_observation(observation)
                except Exception, e:
                    rospy.logwarn(str(e))
                    rospy.logwarn("Could not get a static image for logging, "
                                  "there may be a problem.")


#            self._status_publisher.publish(String("stop_viewing"))
            return "done"
        except Exception, e:
            print e
            self._status_publisher.publish(String("stop_viewing"))
            return "error"
