#pragma once
#include "global.h"

struct camera {
	glm::vec3 origin; // lookat
	float radius;
	glm::vec3 world_up;
	int width, height;
	float fov;
	float near, far;
	float phi, theta;

	glm::vec3 position, direction, up, right;

	camera() {}
	camera(
		const glm::vec3 &origin,
		float radius,
		const glm::vec3 &world_up,
		int width,
		int height,
		float fov,
		float near,
		float far,
		float phi,
		float theta
	) :
		origin(origin),
		radius(radius),
		world_up(world_up),
		width(width),
		height(height),
		fov(fov),
		near(near),
		far(far),
		phi(phi),
		theta(theta)
	{
		update(phi, theta);
	}

	void update(float phi, float theta) {
		float x, y, z;
		y = radius * glm::cos(glm::radians(phi));
		x = radius * glm::sin(glm::radians(phi)) * glm::sin(glm::radians(theta));
		z = radius * glm::sin(glm::radians(phi)) * glm::cos(glm::radians(theta));

		position = glm::vec3(x, y, z);
		direction = glm::normalize(origin - position);
		right = glm::normalize(glm::cross(direction, world_up));
		up = glm::normalize(glm::cross(right, direction));
	}

	glm::mat4 view() {
		return glm::lookAt(position, origin, world_up);
	}

	glm::mat4 projection() {
		return glm::perspective(glm::radians(fov), (float)width / (float)height, near, far);
	}

};