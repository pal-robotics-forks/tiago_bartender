from __future__ import print_function

import rospy
import actionlib
from bitbots_stackmachine.abstract_action_element import AbstractActionElement
from tiago_bartender_msgs.msg import PourGoal, PickGoal, TakeOrderGoal, MoveToTargetGoal, DetectBottlesGoal
from random import uniform
from geometry_msgs.msg import PointStamped
from move_base_msgs.msg import MoveBaseGoal
from std_msgs.msg import Bool
from pal_interaction_msgs.msg import TtsGoal
from actionlib_msgs.msg import GoalStatus

class IdleMoveAround(AbstractActionElement):
    """
    move to random pose behind the counter
    """
    def __init__(self, blackboard, _):
        super(IdleMoveAround, self).__init__(blackboard)
        self.first_iteration = True

        self.goal = MoveBaseGoal()
        self.goal.target_pose.header.frame_id = blackboard.idle_zone['frame']
        self.goal_x = uniform(blackboard.idle_zone['center_x'] - blackboard.idle_zone['radius_x'], blackboard.idle_zone['center_x'] + blackboard.idle_zone['radius_x'])
        self.goal_y = uniform(blackboard.idle_zone['center_y'] - blackboard.idle_zone['radius_y'], blackboard.idle_zone['center_y'] + blackboard.idle_zone['radius_y'])
        self.goal.target_pose.pose.position.x = goal_x
        self.goal.target_pose.pose.position.y = goal_y
        self.goal.target_pose.pose.orientation.z = 1.0

    def perform(self, blackboard, reevaluate=False):
        print("IdleMoveAround")
        if self.first_iteration:
            blackboard.move_base_client.send_goal(self.goal)
            self.first_iteration = False

        state = blackboard.move_base_client.get_state()
        # wait till action is completed
        if state == GoalStatus.SUCCEEDED:
            self.pop()


class WaitForRos(AbstractActionElement):
    def __init__(self, blackboard, args=''):
        super(WaitForRos, self).__init__(blackboard)
        self.first_iteration = True
        self.name = args
    def perform(self):
        rospy.loginfo_throttle(10, "Waiting for server %s", self.name)
        self.pop()


class WaitingToResume(AbstractActionElement):
    """
    This action doesn't do anything and is only put on the stack for visualization purposes
    """
    def perform(self, blackboard, reevaluate=False):
        rospy.loginfo_throttle(10, "I'm currently paused. Please show pause card to unpause me.")


class MoveToCustomer(AbstractActionElement):
    #TODO: update customer position for look at service
    def __init__(self, blackboard, _):
        super(IdleMoveAround, self).__init__(blackboard)
        self.first_iteration = False
        self.goal = MoveBaseGoal()
        self.goal.target_pose.header.frame_id = blackboard.take_order_pose['frame']
        self.goal.target_pose.pose.position.x = blackboard.take_order_pose['pos_x']
        self.goal.target_pose.pose.position.y = blackboard.take_order_pose['pos_y']
        self.goal.target_pose.pose.orientation.x = blackboard.take_order_pose['ori_x']
        self.goal.target_pose.pose.orientation.y = blackboard.take_order_pose['ori_y']
        self.goal.target_pose.pose.orientation.z = blackboard.take_order_pose['ori_z']
        self.goal.target_pose.pose.orientation.w = blackboard.take_order_pose['ori_w']

    def perform(self, blackboard, reevaluate=False):
        print("MoveToCustomer")
        if self.first_iteration:
            blackboard.move_base_action_client.send_goal(self.goal)
            self.first_iteration = False

        state = blackboard.move_base_action_client.get_state()
        # wait till action is completed
        if state == GoalStatus.SUCCEEDED:
            blackboard.arrived_at_customer = True

class MoveToBottle(AbstractActionElement):
    """
    Moves to the pose to put down the last bottle of the recipe
    """
    def __init__(self, blackboard, _):
        super(MoveToBottle, self).__init__(blackboard)
        self.first_iteration = True
        blackboard.arrived_at_bottle = False
        self.goal = MoveToTargetGoal()
        self.goal.target = blackboard.current_bottle
        self.goal.look_at_target = False

    def perform(self, blackboard, reevaluate=False):
        if self.first_iteration:
            blackboard.move_action_client.send_goal(self.goal)
            self.first_iteration = False

        state = blackboard.move_action_client.get_state()
        # wait till action is completed
        if state == GoalStatus.SUCCEEDED:
            blackboard.arrived_at_bottle = True

class MoveToPouringPosition(AbstractActionElement):
    """
    Moves to the pose to put down the last bottle of the recipe
    """
    def __init__(self, blackboard, _):
        super(MoveToPouringPosition, self).__init__(blackboard)
        self.first_iteration = True
        blackboard.arrived_at_pouring_position = False
        self.goal = MoveToTargetGoal()
        self.goal.target = 'glass'
        self.goal.look_at_target = False

    def perform(self, blackboard, reevaluate=False):
        if self.first_iteration:
            blackboard.move_action_client.send_goal(self.goal)
            self.first_iteration = False

        state = blackboard.move_action_client.get_state()
        # wait till action is completed
        if state == GoalStatus.SUCCEEDED:
            blackboard.arrived_at_pouring_position = True

class AbstractLookAt(AbstractActionElement):
    def perform(self, blackboard, reevaluate=False):
        target = self.target()
        pose = PointStamped()
        print("Looking at " + target)
        try:
            blackboard.look_at_service(target, pose)
        except rospy.ServiceException, e:
            print("Service call failed: %s"%e)
        self.pop()

    def target(self):
        raise NotImplementedError


class LookAtCustomer(AbstractActionElement):
    def perform(self, blackboard, reevaluate=False):
        target = "customer"
        pose = blackboard.customer_position
        print("Looking at " + target)
        try:
            blackboard.look_at_service(target, pose)
        except rospy.ServiceException, e:
            print("Service call failed: %s"%e)
        self.pop()

class LookAtBottle(AbstractLookAt):
    def target(self):
        return blackboard.current_bottle

class LookAtMenu(AbstractLookAt):
    def target(self):
        return "down"

class LookForward(AbstractLookAt):
    def target(self):
        return "forward"

class LookForCustomer(AbstractActionElement):
    def __init__(self, blackboard, _):
        super(LookForCustomer, self).__init__(blackboard)
        self.first_iteration = True

    def perform(self, blackboard, reevaluate=False):
        if first_iteration:
            target = 'look_around'
            pose = PointStamped()
            try:
                blackboard.look_at_service(target, pose)
            except rospy.ServiceException, e:
                print("Service call failed: %s"%e)

            self.begin = rospy.get_rostime()

            enable = Bool()
            enable.data = True
            blackboard.person_detection_switch_pub.publish(enable)
            self.first_iteration = False

        if blackboard.person_detected:
            disable = Bool()
            disable.data = False
            blackboard.person_detection_switch_pub.publish(disable)

            blackboard.customer_position = blackboard.person_position
            blackboard.has_customer = True
            blackboard.person_detected = False
        elif rospy.get_rostime() - self.begin >= rospy.Duration.from_sec(10.0):
            disable = Bool()
            disable.data = False
            blackboard.person_detection_switch_pub.publish(disable)

            self.pop()

class AbstractSay(AbstractActionElement):
    def __init__(self, blackboard, _):
        super(AbstractSay, self).__init__(blackboard)
        self.first_iteration = True
        text = random.choice(self.text())
        print("Saying '" + text + "'")
        self.goal = TtsGoal()
        self.goal.rawtext.text = text
        self.goal.rawtext.lang_id = "en_GB"

    def perform(self, blackboard, reevaluate=False):
        if self.first_iteration:
            blackboard.tts_action_client.send_goal(goal)
            self.first_iteration = False
        state = blackboard.tts_action_client.get_state()
        # wait till action is completed
        if state == 3:
            #TODO maybe do something with the result
            self.pop()

    def text(self):
        raise NotImplementedError

class SayPleaseOrder(AbstractSay):
    def text(self):
        return ["Welcome. You can order by pointing at the drink of your choice on the menu card.",
                "Hello there. Please order by pointing at one of the drinks on the menu card."]

class SayNoMenuFoundRepeat(AbstractSay):
    def text(self):
        return ["I thought I put the menu card here? Could you help me and put the menu card in front of me.", 
                "I was sure the menu card was right here. Could you help me by putting it in front of me.", 
                "Where did I put the menu again? Please help me and put it in front of me, so I can see it."]

class SayRepeatOrder(AbstractSay):
    def text(self):
        return ["Oh, so you want something different?"]

class SayOrderConfirmed(AbstractSay):
    def text(self):
        return ["One " + blackboard.current_drink + " coming right up."]

class SayDrinkFinished(AbstractSay):
    def text(self):
        return ["Here you go!",
                "Enjoy your drink!",
                "Here's your drink!"]

class SayBottleNotFound(AbstractSay):
    def text(self):
        blackboard.bottle_not_found = False
        return ["I was sure I put the " + blackboard.current_bottle + " right here. Please help me by putting the bottle in front of me."]

class ObserveOrder(AbstractActionElement):
    def __init__(self, blackboard, _):
        super(ObserveOrder, self).__init__(blackboard)
        self.first_iteration = True
        self.goal = TakeOrderGoal()
        self.goal.timeout = rospy.Duration.from_sec(20)

    def perform(self, blackboard, reevaluate=False):
        if self.first_iteration:
            blackboard.take_order_action_client.send_goal(self.goal)
            self.first_iteration = False

        # if no result yet
        if !blackboard.take_order_action_client.wait_for_result(rospy.Duration.from_sec(0.01)):
            return
        blackboard.no_menu_found = False
        result = blackboard.take_order_action_client.get_result()
        if result.status == "timeout":
            self.pop()
        elif result.status == "no_menu_card_detected":
            blackboard.no_menu_found = True
        else:
            blackboard.current_drink = result.selection
            blackboard.recipe = blackboard.recipes[result.selection]

class UpdateBottlePose(AbstractActionElement):
    def __init__(self, blackboard, _):
        super(UpdateBottlePose, self).__init__(blackboard)
        self.first_iteration = True
        self.goal = DetectBottlesGoal()
        self.goal.timeout = rospy.Duration.from_sec(10)
        self.goal.stability_threshold = 5

    def perform(self, blackboard, reevaluate=False):
        if self.first_iteration:
            blackboard.detect_bottles_action_client.send_goal(self.goal)
            self.first_iteration = False
        # if no result yet
        if !blackboard.detect_bottles_action_client.wait_for_result(rospy.Duration.from_sec(0.01)):
            return

        blackboard.bottle_located = False
        result = blackboard.detect_bottles_action_client.get_result()
        if blackboard.current_bottle in result.detected_bottles:
            blackboard.bottle_located = True
            self.pop()
        elif result.status == "no_menu_card_detected":
            blackboard.no_menu_found = True
        else:
            blackboard.current_drink = result.selection
            blackboard.recipe = blackboard.recipes[result.selection]

class PickUpBottle(AbstractActionElement):
    """
    Calls the pick action
    """
    def __init__(self, blackboard, _):
        super(PickUpBottle, self).__init__(blackboard)
        goal = Pick()
        #TODO specify goal
        blackboard.pick_action_client.send_goal(goal)

    def perform(self, blackboard, reevaluate=False):
        state = blackboard.pick_action_client.get_state()
        # wait till action is completed
        if state == 3:
            #TODO maybe do something with the result
            self.pop()

class PourLiquid(AbstractActionElement):
    """
    Calls the pouring action
    """
    def __init__(self, blackboard, _):
        super(PourLiquid, self).__init__(blackboard)
        goal = Pour()
        #TODO specify goal
        blackboard.pour_action_client.send_goal(goal)

    def perform(self, blackboard, reevaluate=False):
        state = blackboard.pour_action_client.get_state()
        # wait till action is completed
        if state == 3:
            #TODO maybe do something with the result
            self.pop()

class PlaceBottle(AbstractActionElement):
    """
    Calls the pouring action
    """
    def __init__(self, blackboard, _):
        super(PlaceBottle, self).__init__(blackboard)
        goal = Place()
        #TODO specify goal
        blackboard.place_action_client.send_goal(goal)

    def perform(self, blackboard, reevaluate=False):
        state = blackboard.place_action_client.get_state()
        # wait till action is completed
        if state == 3:
            #TODO maybe do something with the result
            self.pop()

class MoveToBottlePose(AbstractActionElement):
    """
    Moves to the pose to put down the last bottle of the recipe
    """
    def __init__(self, blackboard, _):
        super(MoveToBottlePose, self).__init__(blackboard)
        self.first_iteration = True
        blackboard.arrived_at_bottle = False
        self.goal = MoveToTargetGoal()
        self.goal.target = ''
        self.goal.target_pose = blackboard.last_bottle_pose
        self.goal.look_at_target = False

    def perform(self, blackboard, reevaluate=False):
        if self.first_iteration:
            blackboard.move_action_client.send_goal(self.goal)
            self.first_iteration = False

        state = blackboard.move_action_client.get_state()
        # wait till action is completed
        if state == GoalStatus.SUCCEEDED:
            blackboard.arrived_at_bottle = True

class Wait(AbstractActionElement):
    """
    Waits the amount of seconds given on init and pops
    """
    def __init__(self, blackboard, args=10):
        super(Wait, self).__init__(blackboard)
        self.resume_time = rospy.get_time() + args

    def perform(self, connector, reevaluate=False):       
        if self.resume_time < rospy.get_time():
            self.pop()        
