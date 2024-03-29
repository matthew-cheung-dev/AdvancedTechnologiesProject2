#include "Ray.h"

#include "Geometry.h"
#include "Light.h"
#include "BoundingVolumeHierachy.h"

#include <iostream>

Ray::Ray(const glm::vec3 & rayOrig, const glm::vec3 & rayDir, unsigned int depth)
	: m_rayOrig(rayOrig), m_rayDirection(rayDir), m_depth(depth)
{
	m_invDirection = 1.0f / m_rayDirection;

	m_sign[0] = (m_invDirection.x < 0);
	m_sign[1] = (m_invDirection.y < 0);
	m_sign[2] = (m_invDirection.z < 0);
}

const bool Ray::trace(const std::vector<std::shared_ptr<Geometry>> &objects)
{
	for (auto& obj : objects)
	{
		float tNearK = kInfinity;
		std::uint64_t indexK;
		glm::vec2 uvK;
		float t;

		if (obj->getBox().checkRayCollision(this))
		{
			if (obj->intersect(this, indexK, uvK, t))
			{
				m_hitObject = obj.get();
				m_index = indexK;
				m_uv = uvK;
			}
		}
	}

	return (m_hitObject != nullptr);
}

const bool Ray::trace(std::shared_ptr<BVH>& bvh)
{
	bvh->checkIntersection(this, &m_hitObject, m_index, m_uv);
	return (m_hitObject != nullptr);
}

const glm::vec3 Ray::mix(const glm::vec3 & a, const glm::vec3 & b, const float & mixValue)
{
	return a * (1 - mixValue) + b * mixValue;
}

glm::vec3 Ray::castRay(const std::vector<std::shared_ptr<Geometry>>& shapes,
					   const std::vector<std::shared_ptr<Light>>& lights,
					   ImageData data, bool test)
{
	if (m_depth > data.m_maxDepth)
	{
		return data.m_backgroundColour;
	}

	glm::vec3 hitColour = data.m_backgroundColour;

	if (this->trace(shapes))
	{
		glm::vec3 hitPoint = m_rayOrig  + m_rayDirection * m_closestHit;
		glm::vec3 n;
		glm::vec2 st;

		m_hitObject->getSurface(hitPoint, m_rayDirection, m_index, m_uv, n, st);
		glm::vec3 tmp = hitPoint;

		switch (m_hitObject->getMaterialType())
		{
		case MaterialType::REFLECTION_AND_REFRACTION:
		{
			glm::vec3 reflctionDir = glm::normalize(reflect(m_rayDirection, n));
			glm::vec3 refractionDir = glm::normalize(refract(m_rayDirection, n, m_hitObject->getIOR()));
			glm::vec3 reflectionRayOrig = (glm::dot(reflctionDir, n) < 0) ?
				hitPoint - n * data.m_bias :
				hitPoint + n * data.m_bias;
			glm::vec3 refractionRayOrig = (glm::dot(refractionDir, n) < 0) ?
				hitPoint - n * data.m_bias :
				hitPoint + n * data.m_bias;

			auto reflectionRay = std::make_unique<Ray>(reflectionRayOrig, reflctionDir, m_depth + 1);
			auto refractionRay = std::make_unique<Ray>(refractionRayOrig, refractionDir, m_depth + 1);

			glm::vec3 reflectionColour = reflectionRay->castRay(shapes, lights, data, 1);
			glm::vec3 refractionColour = refractionRay->castRay(shapes, lights, data, 1);

			float kr;
			fresnel(m_rayDirection, n, m_hitObject->getIOR(), kr);
			hitColour = reflectionColour * kr + refractionColour * (1 - kr);
			break;
		}
		case MaterialType::REFLECTION:
		{
			float kr;
			fresnel(m_rayDirection, n, m_hitObject->getIOR(), kr);
			glm::vec3 reflctionDir = glm::normalize(this->reflect(m_rayDirection, n));
			glm::vec3 reflectionRayOrig = (glm::dot(reflctionDir, n) < 0) ?
				hitPoint - n * data.m_bias :
				hitPoint + n * data.m_bias;

			auto reflectionRay = std::make_unique<Ray>(reflectionRayOrig, reflctionDir, m_depth + 1);
			hitColour = reflectionRay->castRay(shapes, lights, data, 1) * kr;

			break;
		}
		case MaterialType::PHONG:
		{
			glm::vec3 diffuse = glm::vec3(0);
			glm::vec3 specular = glm::vec3(0);

			for(auto& light : lights)
			{
				glm::vec3 lightDir;
				glm::vec3 lightIntensity;
				light->illuminate(hitPoint, lightDir, lightIntensity, this->m_closestHit);

				Ray visRay = Ray(hitPoint + n * data.m_bias, -lightDir, m_depth + 1);
				bool vis = !visRay.trace(shapes);
			}
			break;
		}
		default:
		{
			glm::vec3 lightAmt = glm::vec3(0);
			glm::vec3 specularColour = glm::vec3(0);


			auto shadowRay = std::make_unique<Ray>((glm::dot(m_rayDirection, n) < 0) ?
				hitPoint - n * data.m_bias :
				hitPoint + n * data.m_bias, glm::vec3(0), m_depth + 1);

			for (auto& obj : lights)
			{
				glm::vec3 lightDir = glm::normalize(obj->getPos() - hitPoint);

				float lightDistance2 = glm::dot(lightDir, lightDir);
				shadowRay->m_rayDirection = lightDir;
				float lDotN = glm::max(0.0f, glm::dot(lightDir, n));

				bool inShadow = shadowRay->trace(shapes)
					&& shadowRay->m_closestHit * shadowRay->m_closestHit < lightDistance2;

				lightAmt += (1 - inShadow) * obj->getIntensity();
				glm::vec3 reflectionDir = reflect(-lightDir, n);
				specularColour += glm::pow(glm::max(0.0f, -glm::dot(reflectionDir, m_rayDirection)),
										   m_hitObject->getSpecularExponent()) * obj->getIntensity();
			}
			hitColour = lightAmt * m_hitObject->evalDiffuseColour(st) * m_hitObject->getKD() + specularColour * m_hitObject->getKS();
			break;
		}
		}
	}

	return hitColour;
}

glm::vec3 Ray::castRay(std::shared_ptr<BVH> bvh, const std::vector<std::shared_ptr<Light>>& lights, ImageData data, bool test)
{
	if (m_depth > data.m_maxDepth)
	{
		return data.m_backgroundColour;
	}

	glm::vec3 hitColour(0.0f);
	glm::vec2 uv;

	if (this->trace(bvh))
	{
		glm::vec3 hitPoint = m_rayOrig + m_rayDirection * m_closestHit;
		glm::vec3 normal;
		glm::vec2 st;

		m_hitObject->getSurface(hitPoint, m_rayDirection, m_index, uv, normal, st);

		switch(m_hitObject->getMaterialType())
		{
			case MaterialType::REFLECTION_AND_REFRACTION:
			{
				glm::vec3 reflctionDir = glm::normalize(reflect(m_rayDirection, normal));
				glm::vec3 refractionDir = glm::normalize(refract(m_rayDirection, normal, m_hitObject->getIOR()));
				glm::vec3 reflectionRayOrig = (glm::dot(reflctionDir, normal) < 0) ?
					hitPoint - normal * data.m_bias :
					hitPoint + normal * data.m_bias;
				glm::vec3 refractionRayOrig = (glm::dot(refractionDir, normal) < 0) ?
					hitPoint - normal * data.m_bias :
					hitPoint + normal * data.m_bias;

				auto reflectionRay = std::make_unique<Ray>(reflectionRayOrig, reflctionDir, m_depth + 1);
				auto refractionRay = std::make_unique<Ray>(refractionRayOrig, refractionDir, m_depth + 1);

				glm::vec3 reflectionColour = reflectionRay->castRay(bvh, lights, data, 1);
				glm::vec3 refractionColour = refractionRay->castRay(bvh, lights, data, 1);

				float r;
				fresnel(m_rayDirection, normal, m_hitObject->getIOR(), r);
				hitColour = reflectionColour * r + refractionColour * (1 - r);
				break;
			}
			case MaterialType::REFLECTION:
			{
				float r;
				fresnel(m_rayDirection, normal, m_hitObject->getIOR(), r);
				glm::vec3 reflctionDir = glm::normalize(this->reflect(m_rayDirection, normal));
				glm::vec3 reflectionRayOrig = (glm::dot(reflctionDir, normal) < 0) ?
					hitPoint - normal * data.m_bias :
					hitPoint + normal * data.m_bias;

				auto reflectionRay = std::make_unique<Ray>(reflectionRayOrig, reflctionDir, m_depth + 1);
				hitColour = reflectionRay->castRay(bvh, lights, data, 1) * r;

				break;
			}
			case MaterialType::PHONG:
			{
				glm::vec3 diffuse = glm::vec3(0);
				glm::vec3 specular = glm::vec3(0);

				for(auto& light : lights)
				{
					glm::vec3 lightDir;
					glm::vec3 lightIntensity;
					light->illuminate(hitPoint, lightDir, lightIntensity, this->m_closestHit);

					Ray visRay = Ray(hitPoint + normal * data.m_bias, -lightDir, m_depth + 1);
					bool vis = !visRay.trace(bvh);

					float dot = glm::dot(-lightDir, normal);
					diffuse += m_hitObject->getAlbedo() * light->getIntensity() * glm::max(0.0f, glm::dot(normal, -lightDir));

					glm::vec3 r = reflect(lightDir, normal);
					specular += float(vis) * lightIntensity * glm::pow(glm::max(0.0f, glm::dot(r, m_rayDirection)), m_hitObject->getSpecularExponent());
				}
				hitColour = diffuse * m_hitObject->getKD() * m_hitObject->getDiffuseColour() + specular * m_hitObject->getKS();
				break;
			}
			case MaterialType::DIFFUSE:
			{
				//Calculate light
				for(auto& light : lights)
				{
					glm::vec3 lightDir;
					glm::vec3 lightIntensity;
					light->illuminate(hitPoint, lightDir, lightIntensity, this->m_closestHit);

					Ray visRay(hitPoint + normal * data.m_bias, -lightDir, m_depth + 1);
					bool vis = !visRay.trace(bvh);

					float angle = glm::radians(45.0f);
					float s = m_uv.x * glm::cos(angle) - m_uv.y * glm::sin(angle);
					float t = m_uv.y * glm::cos(angle) + m_uv.x * glm::sin(angle);

					float pattern = (glm::floor(s * 20.0f) < 0.5f);
					hitColour += float(vis) * m_hitObject->evalDiffuseColour(st) * light->getIntensity() * glm::max(0.0f, glm::dot(normal, -lightDir));
				}
				break;
			}
			default:
			{
				glm::vec3 lightAmt = glm::vec3(0);
				glm::vec3 specularColour = glm::vec3(0);


				auto shadowRay = std::make_unique<Ray>((glm::dot(m_rayDirection, normal) < 0) ?
													   hitPoint - normal * data.m_bias :
													   hitPoint + normal * data.m_bias, glm::vec3(0), m_depth + 1);

				//Calculate light
				for(auto& obj : lights)
				{
					glm::vec3 lightDir(0.0f);
					glm::vec3 intensity(0.0f);
					float distance;
					obj->illuminate(hitPoint, lightDir, intensity, distance);

					float lightDistance2 = glm::dot(lightDir, lightDir);
					shadowRay->m_rayDirection = lightDir;
					float lDotN = glm::max(0.0f, glm::dot(lightDir, normal));

					bool inShadow = shadowRay->trace(bvh)
						&& shadowRay->m_closestHit * shadowRay->m_closestHit < lightDistance2;

					lightAmt += (1 - inShadow) * obj->getIntensity() * lDotN;
					glm::vec3 reflectionDir = reflect(-lightDir, normal);
					specularColour += glm::pow(glm::max(0.0f, -glm::dot(reflectionDir, m_rayDirection)), m_hitObject->getSpecularExponent()) * obj->getIntensity();
				}
				hitColour = lightAmt * m_hitObject->evalDiffuseColour(st) * m_hitObject->getKD() + specularColour * m_hitObject->getKS();
				break;
			}
		}
	}
	else
	{
		hitColour = data.m_backgroundColour;
	}

	return hitColour;
}

glm::vec3 Ray::reflect(const glm::vec3& i, const glm::vec3& n)
{
	return i - 2 * glm::dot(i, n) * n;
}

glm::vec3 Ray::refract(const glm::vec3 & i, const glm::vec3 & n, const float & ior)
{
	float cosi = glm::clamp(glm::dot(i, n), -1.0f, 1.0f);
	float etai = 1;
	float etat = ior;

	glm::vec3 normal = n;

	if (cosi < 0)
	{
		cosi = -cosi;
	}
	else
	{
		std::swap(etai, etat);
		normal = -n;
	}

	float eta = etai / etat;
	float k = 1 - eta * eta * (1 - cosi * cosi);
	glm::vec3 temp;
	if (k < 0)
	{
		temp = glm::vec3(0);
	}
	else
	{
		temp = eta * i + (eta * cosi - glm::sqrt(k)) * n;
	}

	return temp;
}

void Ray::fresnel(const glm::vec3 & i, glm::vec3 & n, const float & ior, float & kr)
{
	float cosi = glm::clamp(glm::dot(i, n), -1.0f, 1.0f);
	float etai = 1;
	float etat = ior;

	if (cosi > 0)
	{
		std::swap(etai, etat);
	}

	float sint = etai / etat * glm::sqrt(glm::max(0.0f, 1 - cosi * cosi));

	if (sint >= 1)
	{
		kr = 1;
	}
	else
	{
		float cost = glm::sqrt(std::max(0.0f, 1 - sint * sint));
		cosi = fabsf(cosi);
		float rs = ((etat*cosi) - (etai * cost)) / ((etat * cosi) + (etai* cost));
		float rp = ((etai* cosi) - (etat * cost)) / ((etai * cosi) + (etat*cost));
		kr = (rs * rs + rp * rp) / 2;
	}
}
