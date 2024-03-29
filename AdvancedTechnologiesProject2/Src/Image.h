#pragma once

#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>

#include <glm/glm.hpp>

#include <future>
#include <vector>
#include <thread>
#include <atomic>

class Geometry;
class Light;
class Camera;
class BVH;

struct ImageData
{
	glm::u64vec2 m_size;
	std::uint8_t m_maxDepth = 5;
	glm::vec3 m_backgroundColour = glm::vec3(1.0f, 1.0f, 1.0f);
	float m_bias = (float)0.00001;
	float m_aspectRatio;
	float m_fov;
};

class Image
{
public:
	Image(Camera* camera, unsigned int sizeX = 640, unsigned int sizeY = 480);
	~Image() = default;

	void putPixel(glm::u64vec2 pos, glm::vec4 colour);

	void render(Camera* camera, const std::vector<std::shared_ptr<Geometry>>& shapes,
				const std::vector<std::shared_ptr<Light>>& lights);
	void render(Camera* camera,std::shared_ptr<BVH>& bvh,
				const std::vector<std::shared_ptr<Light>>& lights, std::atomic<sf::RenderWindow*>& window);

	bool createImage();

	bool exportImage();
	void draw(std::atomic<sf::RenderWindow*>& renderWindow);

	const glm::u64vec2& getSize() const { return m_imageData.m_size; }
private:
	ImageData m_imageData;
	std::vector<std::shared_ptr<glm::vec4>> m_pixels;

	std::vector<std::future<void>> m_future_vector;

	std::atomic<sf::Image*> m_image; 
	std::atomic<sf::Texture*> m_texture;
	std::atomic<sf::Sprite*> m_sprite;
};