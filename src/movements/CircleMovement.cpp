/*
 * Copyright (C) 2006-2013 Christopho, Solarus - http://www.solarus-games.org
 * 
 * Solarus is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Solarus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "movements/CircleMovement.h"
#include "lua/LuaContext.h"
#include "lowlevel/System.h"
#include "lowlevel/Geometry.h"
#include "lowlevel/Debug.h"
#include "lowlevel/StringConcat.h"
#include "entities/MapEntity.h"
#include "entities/MapEntities.h"
#include "Map.h"

/**
 * \brief Creates a circle movement.
 * \param ignore_obstacles true to ignore obstacles
 */
CircleMovement::CircleMovement(bool ignore_obstacles):

  Movement(ignore_obstacles),
  center_entity(NULL),
  current_angle(0),
  initial_angle(0),
  angle_increment(1),
  next_angle_change_date(System::now()),
  angle_change_delay(5),
  current_radius(0),
  wanted_radius(0),
  previous_radius(0),
  next_radius_change_date(System::now()),
  radius_change_delay(0),
  duration(0),
  end_movement_date(0),
  max_rotations(0),
  nb_rotations(0),
  loop_delay(0),
  restart_date(System::now()) {

}

/**
 * \brief Destroys this circle movement.
 */
CircleMovement::~CircleMovement() {

  if (this->center_entity != NULL) {
    this->center_entity->decrement_refcount();
    if (this->center_entity->get_refcount() == 0) {
      delete this->center_entity;
    }
  }
}

/**
 * \brief Sets the center of the circles.
 *
 * The movement will make circles around the specified fixed point.
 *
 * \param center_point center of the circles to make
 */
void CircleMovement::set_center(const Rectangle& center_point) {

  if (this->center_entity != NULL) {
    this->center_entity->decrement_refcount();
    if (this->center_entity->get_refcount() == 0) {
      delete this->center_entity;
    }
  }

  this->center_entity = NULL;
  this->center_point = center_point;
  recompute_position();
}

/**
 * \brief Sets the center of the circles.
 *
 * The movement will make circles around the specified (possibly moving) entity.
 *
 * \param center_entity the entity around which you want to make circles
 * \param x x coordinate of where the center should be placed relative to the entity's origin
 * \param y y coordinate of where the center should be placed relative to the entity's origin
 */
void CircleMovement::set_center(MapEntity& center_entity, int x, int y) {

  if (this->center_entity != NULL) {
    this->center_entity->decrement_refcount();
    if (this->center_entity->get_refcount() == 0) {
      delete this->center_entity;
    }
  }

  this->center_entity = &center_entity;
  this->center_entity->increment_refcount();
  this->center_point.set_xy(x, y);
  recompute_position();
}

/**
 * \brief Returns the radius of the circles.
 * \return the radius in pixels
 */
int CircleMovement::get_radius() const {
  return wanted_radius;
}

/**
 * \brief Sets the radius of the circles.
 * \param radius the radius in pixels
 */
void CircleMovement::set_radius(int radius) {

  Debug::check_assertion(radius >= 0, StringConcat() << "Invalid radius: " << radius);

  this->wanted_radius = radius;
  if (radius_change_delay == 0) {
    if (is_started()) {
      this->current_radius = wanted_radius;
    }
  }
  else {
    this->radius_increment = (radius > this->current_radius) ? 1 : -1;
    if (is_started()) {
      this->next_radius_change_date = System::now();
    }
  }
  recompute_position();
}

/**
 * \brief Returns the speed of the radius variations.
 * \return the speed in pixels per second, or 0 if radius variations are immediate
 */
int CircleMovement::get_radius_speed() const {

  return radius_change_delay == 0 ? 0 : 1000 / radius_change_delay;
}

/**
 * \brief Sets the radius to be updated immediately or gradually towards its wanted value.
 *
 * Use set_radius() to specify the wanted value.
 *
 * \param radius_speed speed of the radius variation (number of pixels per second),
 * or 0 to update it immediately
 */
void CircleMovement::set_radius_speed(int radius_speed) {

  Debug::check_assertion(radius_speed >= 0, StringConcat() << "Invalid radius speed: " << radius_speed);

  if (radius_speed == 0) {
    this->radius_change_delay = 0;
  }
  else {
    this->radius_change_delay = 1000 / radius_speed;
  }

  set_radius(wanted_radius);
}

/**
 * \brief Returns the speed of the angle variation.
 * \return the number of degrees made per second
 */
int CircleMovement::get_angle_speed() const {
  return 1000 / angle_change_delay;
}

/**
 * \brief Sets the speed of the angle variation.
 * \param angle_speed number of degrees to make per second
 */
void CircleMovement::set_angle_speed(int angle_speed) {

  Debug::check_assertion(angle_speed > 0, StringConcat() << "Invalid angle speed: " << angle_speed);

  this->angle_change_delay = 1000 / angle_speed;
  this->next_angle_change_date = System::now();
  recompute_position();
}

/**
 * \brief Returns the angle from where the first circle starts.
 * \return the angle in radians
 */
double CircleMovement::get_initial_angle() const {

  return Geometry::degrees_to_radians(initial_angle);
}

/**
 * \brief Sets the angle from where the first circle starts.
 * \param initial_angle angle in radians
 */
void CircleMovement::set_initial_angle(double initial_angle) {

  Debug::check_assertion(initial_angle >= 0 && initial_angle < Geometry::TWO_PI,
      StringConcat() << "Invalid initial angle: " << initial_angle);

  // convert to degrees (everything works in degrees in this class)
  this->initial_angle = Geometry::radians_to_degrees(initial_angle);
}

/**
 * \brief Returns the direction of the circles.
 * \return true if circles are clockwise
 */
bool CircleMovement::is_clockwise() const {

  return angle_increment < 0;
}

/**
 * \brief Sets the direction of circles.
 * \param clockwise true to make clockwise circles
 */
void CircleMovement::set_clockwise(bool clockwise) {

  this->angle_increment = clockwise ? -1 : 1;
}

/**
 * \brief Returns the maximum duration of the movement.
 *
 * When this delay is reached, the movement stops.
 * Note that if the radius changes gradually, the movement will continue
 * for a while until the radius reaches zero.
 *
 * \return duration of the movement in milliseconds, (0 means infinite)
 */
uint32_t CircleMovement::get_duration() const {

  return duration;
}

/**
 * \brief Sets the maximum duration of the movement.
 *
 * When this delay is reached, the movement stops.
 * Note that if the radius changes gradually, the movement will continue
 * for a while until the radius reaches zero.
 *
 * \param duration duration of the movement in milliseconds, or 0 to make it infinite
 */
void CircleMovement::set_duration(uint32_t duration) {

  this->duration = duration;
  if (duration != 0 && is_started()) {
    this->end_movement_date = System::now() + duration;
  }
}

/**
 * \brief Returns the number of rotations of the movement.
 *
 * When this number of rotations is reached, the movement stops.
 * Note that if the radius changes gradually, the movement will continue
 * for a while until the radius reaches zero.
 *
 * \return the number of rotations to make (0 means infinite rotations)
 */
int CircleMovement::get_max_rotations() const {

  return max_rotations;
}

/**
 * \brief Sets the number of rotations of the movement.
 *
 * When this number of rotations is reached, the movement stops.
 * Note that is the radius changes gradually, the movement will continue
 * for a while until the radius reaches zero.
 *
 * \param max_rotations number of rotations to make (0 for infinite rotations)
 */
void CircleMovement::set_max_rotations(int max_rotations) {

  Debug::check_assertion(max_rotations >= 0, StringConcat() << "Invalid maximum rotations number: " << max_rotations);

  this->max_rotations = max_rotations;
  this->nb_rotations = 0;
}

/**
 * \return Returns the delay after which the movement restarts.
 * \return the delay in milliseconds (0 means no restart)
 */
uint32_t CircleMovement::get_loop() const {

  return loop_delay;
}

/**
 * \brief Makes the movement restart after a delay when it is finished.
 * \param delay the movement will restart after this delay in milliseconds (0 to avoid this)
 */
void CircleMovement::set_loop(uint32_t delay) {

  this->loop_delay = delay;
  if (delay != 0 && is_stopped()) {
    this->restart_date = System::now() + delay;
  }
}

/**
 * \brief Updates the movement.
 */
void CircleMovement::update() {

  if (center_entity != NULL && center_entity->is_being_removed()) {
    set_center(Rectangle(
          center_entity->get_x() + center_point.get_x(),
          center_entity->get_y() + center_point.get_y()));
  }

  if (is_suspended()) {
    return;
  }

  bool update_needed = false;
  uint32_t now = System::now();

  // maybe it is time to stop or to restart
  if (current_radius != 0 && duration != 0 && now >= end_movement_date && wanted_radius != 0) {
    stop();
  }
  else if (current_radius == 0 && loop_delay != 0 && now >= restart_date && wanted_radius == 0) {
    set_radius(previous_radius);
    start();
  }

  // update the angle
  if (is_started()) {
    while (now >= next_angle_change_date) {

      current_angle += angle_increment;
      current_angle = (360 + current_angle) % 360;
      if (current_angle == initial_angle) {
        nb_rotations++;

        if (nb_rotations == max_rotations) {
          stop();
        }
      }

      next_angle_change_date += angle_change_delay;
      update_needed = true;
    }
  }

  // update the radius
  while (current_radius != wanted_radius && now >= next_radius_change_date) {

    current_radius += radius_increment;

    next_radius_change_date += radius_change_delay;
    update_needed = true;
  }

  // the center may have moved
  if (center_entity != NULL) {
    update_needed = true;
  }

  if (update_needed) {
    recompute_position();
  }

  // Do this at last so that Movement::update() knows whether we are finished.
  Movement::update();
}

/**
 * \brief Computes the position of the object controlled by this movement.
 *
 * This function should be called whenever the angle, the radius or the center changes.
 */
void CircleMovement::recompute_position() {

  Rectangle center = this->center_point;
  if (center_entity != NULL) {
    center.add_xy(center_entity->get_xy());
  }

  const Rectangle &xy = Geometry::get_xy(center, Geometry::degrees_to_radians(current_angle), current_radius);
  if (get_entity() == NULL
      || !test_collision_with_obstacles(xy.get_x() - get_entity()->get_x(), xy.get_y() - get_entity()->get_y())) {
    set_xy(xy);
    notify_position_changed();
  }
  else {
    notify_obstacle_reached();
  }
}

/**
 * \brief Suspends or resumes the movement
 * \param suspended true to suspend the movement, false to resume it
 */
void CircleMovement::set_suspended(bool suspended) {

  Movement::set_suspended(suspended);

  if (get_when_suspended() != 0) {
    uint32_t diff = System::now() - get_when_suspended();
    next_angle_change_date += diff;
    next_radius_change_date += diff;
    end_movement_date += diff;
    restart_date += diff;
  }
}

/**
 * \brief Starts the circle movement.
 */
void CircleMovement::start() {

  current_angle = initial_angle;
  next_angle_change_date = System::now();
  nb_rotations = 0;

  if (duration != 0) {
    end_movement_date = System::now() + duration;
  }

  if (radius_change_delay == 0) {
    current_radius = wanted_radius;
  }
  else {
    next_radius_change_date = System::now();
  }
  recompute_position();
}

/**
 * \brief Returns whether this movement is running.
 * \return true if the movement is started
 */
bool CircleMovement::is_started() const {
  return current_radius != 0 || wanted_radius != 0;
}

/**
 * \brief Returns whether this movement is finished.
 * \return true if this movement is finished
 */
bool CircleMovement::is_finished() const {
  return is_stopped();
}

/**
 * \brief Stops the movement.
 */
void CircleMovement::stop() {

  previous_radius = current_radius;
  set_radius(0);

  if (loop_delay != 0) {
    restart_date = System::now() + loop_delay;
  }
  recompute_position();
}

/**
 * \brief Returns the name identifying this type in Lua.
 * \return the name identifying this type in Lua
 */
const std::string& CircleMovement::get_lua_type_name() const {
  return LuaContext::movement_circle_module_name;
}

