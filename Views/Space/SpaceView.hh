#pragma once

#include <raindance/Core/Text.hh>
#include <raindance/Core/Icon.hh>
#include <raindance/Core/Intersection.hh>
#include <raindance/Core/Primitives/Sphere.hh>
#include <raindance/Core/Primitives/Line.hh>
#include <raindance/Core/Primitives/WideLine.hh>
#include <raindance/Core/Camera/Camera.hh>
#include <raindance/Core/Camera/Frustrum.hh>
#include <raindance/Core/Transformation.hh>
#include <raindance/Core/Variables.hh>
#include <raindance/Core/Scene/NodeVector.hh>
#include <raindance/Core/Physics.hh>
#include <raindance/Core/Environment.hh>
#include <raindance/Core/Bezier.hh>
#include <raindance/Core/Octree.hh>

#include "Entities/MVC.hh"

#include "Entities/Graph/GraphModel.hh"
#include "Entities/Graph/GraphMessages.hh"

#include "Views/Space/SpaceNode.hh"
#include "Views/Space/SpaceEdge.hh"
#include "Views/Space/SpaceSphere.hh"

#include "Views/Space/SpaceResources.hh"

typedef TranslationMap<SpaceNode::ID, Node::ID> NodeTranslationMap;
typedef TranslationMap<SpaceEdge::ID, Link::ID> LinkTranslationMap;
#include "Views/Space/SpaceForces.hh"

#include "Pack.hh"

class SpaceRenderer : public OctreeFunctor
{
public:
    SpaceRenderer(GraphContext* context, Camera* camera, Transformation* transformation, int pass)
    : m_Context(context), m_Camera(camera), m_Transformation(transformation), m_Pass(pass)
    {
        m_DrawCount = 0;
    }

    virtual ~SpaceRenderer() {}

    virtual void apply(OctreeElement* element)
    {
        Scene::Node* node = static_cast<Scene::Node*>(element);
        if (node->getMark() != m_Pass)
        {
            node->draw(m_Context, m_Camera->getProjectionMatrix(), m_Camera->getViewMatrix(), m_Transformation->state());
            node->setMark(m_Pass);
            m_DrawCount++;
        }
    }

    inline int getDrawCount() { return m_DrawCount; }

private:
    GraphContext* m_Context;
    Camera* m_Camera;
    Transformation* m_Transformation;
    int m_Pass;
    int m_DrawCount;
};

class SpaceView : public GraphView
{
public:

    enum PhysicsMode { PLAY, PAUSE };

    SpaceView()
    {
        LOG("[SPACEVIEW] Creating space view ...\n");

        g_SpaceResources = new SpaceResources();

        m_WindowWidth = glutGet(GLUT_WINDOW_WIDTH);
        m_WindowHeight = glutGet(GLUT_WINDOW_HEIGHT);

        m_Camera.setPerspectiveProjection(60.0f, (float)m_WindowWidth / (float)m_WindowHeight, 0.1f, 1024.0f);
        m_Camera.lookAt(glm::vec3(0, 0, -5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        m_CameraAnimation = false;

        m_GraphEntity = NULL;

        m_Octree = NULL;
        m_DirtyOctree = false;

        m_PhysicsMode = PAUSE;
        m_LastUpdateTime = 0;
        m_Iterations = 0;
        m_Temperature = 0.2f;
    }

	virtual ~SpaceView()
	{
	    SAFE_DELETE(m_Octree);

		delete g_SpaceResources;
	}

	virtual const char* name() const { return "space"; }

	virtual bool bind(Entity* entity)
	{
	    if (entity->type() != Entity::GRAPH)
	    {
	        LOG("[SPACE] Couldn't bind entity to view : Wrong entity type!\n");
	        return false;
	    }

        m_GraphEntity = static_cast<GraphEntity*>(entity);
        m_LinkAttractionForce.bind(m_GraphEntity->model(), &m_NodeMap);
        m_NodeRepulsionForce.bind(m_GraphEntity->model(), &m_NodeMap);

        m_GraphEntity->views().push_back(this);
        m_GraphEntity->listeners().push_back(this);

        g_SpaceResources->Model = m_GraphEntity->model();

        context()->setCamera(&m_Camera);

        return true;
	}

	void applyDegreeTint()
	{
		std::vector<unsigned int> nodeDegrees;
		nodeDegrees.resize(m_SpaceNodes.size(), 0);
		unsigned int maxDegree = 0;

		Scene::NodeVector::iterator itl;
		for (itl = m_SpaceEdges.begin(); itl != m_SpaceEdges.end(); ++itl)
		{
			if (*itl == NULL)
				continue;

			SpaceEdge* link = static_cast<SpaceEdge*>(*itl);

			SpaceNode::ID node1 = link->getNode1();
			SpaceNode::ID node2 = link->getNode2();

			nodeDegrees[node1]++;
			nodeDegrees[node2]++;

			if (nodeDegrees[node1] > maxDegree)
				maxDegree = nodeDegrees[node1];
			if (nodeDegrees[node2] > maxDegree)
				maxDegree = nodeDegrees[node2];
		}

		for (unsigned int n = 0; n < m_SpaceNodes.size(); n++)
		{
			if (m_SpaceNodes[n] == NULL)
				continue;

			float tint = maxDegree == 0 ? 1.0 : 0.3 + 0.7 * (float) nodeDegrees[n] / (float) maxDegree;

			SpaceNode* node = static_cast<SpaceNode*>(m_SpaceNodes[n]);
			node->setColor(tint * node->getColor());
		}
	}

	virtual IVariable* getAttribute(std::string& name)
	{
        if (name == "debug")
        {
            BooleanVariable* variable = new BooleanVariable();
            variable->set(g_SpaceResources->ShowDebug);
            return variable;
        }

        return NULL;
	}

	virtual IVariable* getNodeAttribute(Node::ID uid, std::string& name)
	{
		SpaceNode::ID id = m_NodeMap.getLocalID(uid);

		if (name == "position")
		{
			Vec3Variable* variable = new Vec3Variable();
			variable->set(m_SpaceNodes[id]->getPosition());
			return variable;
		}
		else if (name == "color")
		{
			Vec4Variable* variable = new Vec4Variable();
			variable->set(static_cast<SpaceNode*>(m_SpaceNodes[id])->getColor());
			return variable;
		}
		else if (name == "locked")
		{
			BooleanVariable* variable = new BooleanVariable();
			variable->set(m_SpaceNodes[id]->isPositionLocked());
			return variable;
		}
		else if (name == "activity")
		{
			FloatVariable* variable = new FloatVariable();
			variable->set(static_cast<SpaceNode*>(m_SpaceNodes[id])->getActivity());
			return variable;
		}
        else if (name == "mark")
        {
            IntVariable* variable = new IntVariable();
            variable->set(static_cast<SpaceNode*>(m_SpaceNodes[id])->getMark());
            return variable;
        }
        else if (name == "size")
        {
            FloatVariable* variable = new FloatVariable();
            variable->set(static_cast<SpaceNode*>(m_SpaceNodes[id])->getSize());
            return variable;
        }

		return NULL;
	}

	virtual IVariable* getLinkAttribute(Link::ID uid, std::string& name)
	{
		SpaceEdge::ID id = m_LinkMap.getLocalID(uid);

		if (name == "activity")
		{
			FloatVariable* variable = new FloatVariable();
			variable->set(static_cast<SpaceEdge*>(m_SpaceEdges[id])->getActivity());
			return variable;
		}
		else if (name == "color1")
		{
		    Vec4Variable* variable = new Vec4Variable();
		    variable->set(static_cast<SpaceEdge*>(m_SpaceEdges[id])->getColor(0));
		    return variable;
		}
		else if (name == "color2")
		{
            Vec4Variable* variable = new Vec4Variable();
            variable->set(static_cast<SpaceEdge*>(m_SpaceEdges[id])->getColor(1));
            return variable;
		}

		return NULL;
	}

	void draw()
	{
	    const glm::vec4 bgcolor = glm::vec4(BLACK, 1.0);
        glClearColor(bgcolor.r, bgcolor.g, bgcolor.b, bgcolor.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		#ifndef EMSCRIPTEN // NOTE : WebGL doesn't like rectangle images
        	g_SpaceResources->m_Wallpaper->draw(context());
        #endif

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_DST_ALPHA);

        // NOTE : In the future, we want to disable the depth test, and render the image by layers.
        // However this means we need to sort the nodes by distance to the eye and use the Painter's algorithm

		Transformation transformation;

		// Draw Nodes
		{
            if (m_Octree == NULL || m_DirtyOctree)
            {
                m_SpaceNodes.draw(context(), m_Camera.getProjectionMatrix(), m_Camera.getViewMatrix(), transformation.state());

                // Draw Edges
                if (g_SpaceResources->ShowEdges || g_SpaceResources->ShowEdgeActivity)
                {
        			#ifndef EMSCRIPTEN
	                    // NOTE : Not supported by WebGL
	                    glEnable(GL_LINE_SMOOTH);
	                    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
        			#endif

                    m_SpaceEdges.draw(context(), m_Camera.getProjectionMatrix(), m_Camera.getViewMatrix(), transformation.state());
                }
            }
            else
            {
                static int pass = 0;
                pass = (pass + 1) % 2;
                SpaceRenderer renderer(context(), &m_Camera, &transformation, pass);
                Frustrum frustrum(m_Camera.getViewProjectionMatrix());
                m_Octree->foreachElementsInsideFrustrum(frustrum, &renderer);

                static int drawCount = 0;
                if (drawCount != renderer.getDrawCount())
                {
                    drawCount  = renderer.getDrawCount();
                    if (g_SpaceResources->ShowDebug)
                    {
                        LOG("[DEBUG] %i elements drawn.\n", drawCount);
                    }
                }

                if (g_SpaceResources->ShowDebug)
                    m_Octree->draw(context(), m_Camera.getProjectionMatrix(), m_Camera.getViewMatrix(), transformation.state());
            }

            std::set<Node::ID>::iterator iti;
            for (iti = model()->selectedNodes_begin(); iti != model()->selectedNodes_end(); ++iti)
            {
                SpaceNode::ID selectedID = m_NodeMap.getLocalID(*iti);
                SpaceNode* selectedNode = static_cast<SpaceNode*>(m_SpaceNodes[selectedID]);

                float iconSize = 2.0f * selectedNode->getSize() * g_SpaceResources->NodeIconSize;
                glm::mat4 modelView = m_Camera.getViewMatrix() * glm::translate(transformation.state(), m_SpaceNodes[selectedID]->getPosition());
                modelView = glm::scale(Geometry::billboard(modelView), glm::vec3(iconSize, iconSize, iconSize));
                g_SpaceResources->NodeTargetIcon->draw(context(), m_Camera.getProjectionMatrix() * modelView, glm::vec4(1.0, 1.0, 1.0, 1.0), 0);
            }
		}

		// Draw spheres
		if (g_SpaceResources->ShowSpheres)
			m_SpaceSpheres.draw(context(), m_Camera.getProjectionMatrix(), m_Camera.getViewMatrix(), transformation.state());
	}

	void computeBoundingSphere(Sphere& sphere, glm::vec3* center, float* radius)
	{
		glm::vec3 barycenter = glm::vec3(0, 0, 0);
		float r = 1.0;

		for (unsigned int n = 0; n < sphere.data().Nodes.size(); n++)
		{
			barycenter = barycenter + m_SpaceNodes[sphere.data().Nodes[n]]->getPosition() / (float) sphere.data().Nodes.size();
		}

		for (unsigned int n = 0; n < sphere.data().Nodes.size(); n++)
		{
			float length = glm::length(barycenter - m_SpaceNodes[sphere.data().Nodes[n]]->getPosition());
			if (length > r)
				r = length;
		}

		*radius = r;
		*center = barycenter;
	}

	SpaceNode::ID pushNodeVertexAround(Node::ID uid, const char* label, glm::vec3 position, float radius)
	{
		float rnd1 = (float) rand();
		float rnd2 = (float) rand();
		float rnd3 = 0.8f + ((float) rand() / RAND_MAX) / 5;

		SpaceNode* node = new SpaceNode(label);
		unsigned long vid = m_SpaceNodes.add(node);
		node->setID(vid);
		node->setPosition(position + glm::vec3(radius * rnd3 * sin(rnd1) * cos(rnd2),
											   radius * rnd3 * cos(rnd1),
											   radius * rnd3 * sin(rnd1) * sin(rnd2)));

		m_NodeMap.addRemoteID(uid, vid);

		return vid;
	}

	void notify(IMessage* message)
	{
		IMessage::Type type = message->type();

		if (type == IMessage::WIDGET)
		{
			WidgetMessage* msg = static_cast<WidgetMessage*>(message);

			if (msg->Message == "play")
			{
				m_Iterations = 1;
				m_PhysicsMode = PLAY;
			}
			else if (msg->Message == "pause")
			{
				m_PhysicsMode = PAUSE;
			}
			else if (msg->Message == "space:nodes:all")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::ALL;
				g_SpaceResources->ShowNodeLabels = true;
			}
			else if (msg->Message == "space:nodes:shapes+labels")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::COLORS;
				g_SpaceResources->ShowNodeLabels = true;
			}
			else if (msg->Message == "space:nodes:marks+labels")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::MARKS;
				g_SpaceResources->ShowNodeLabels = true;
			}
			else if (msg->Message == "space:nodes:shapes+marks")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::ALL;
				g_SpaceResources->ShowNodeLabels = false;
			}
			else if (msg->Message == "space:nodes:shapes")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::COLORS;
				g_SpaceResources->ShowNodeLabels = false;
			}
			else if (msg->Message == "space:nodes:marks")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::MARKS;
				g_SpaceResources->ShowNodeLabels = false;
			}
			else if (msg->Message == "space:nodes:labels")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::NONE;
				g_SpaceResources->ShowNodeLabels = true;
			}
			else if (msg->Message == "space:nodes:off")
			{
				g_SpaceResources->ShowNodeShapes = SpaceResources::NONE;
				g_SpaceResources->ShowNodeLabels = false;
			}
			else if (msg->Message == "space:edges:lines")
				g_SpaceResources->m_EdgeMode = SpaceResources::LINES;
            else if (msg->Message == "space:edges:widelines")
                g_SpaceResources->m_EdgeMode = SpaceResources::WIDE_LINES;
            else if (msg->Message == "space:edges:off")
                g_SpaceResources->m_EdgeMode = SpaceResources::OFF;


			else if (msg->Message == "show spheres")
				g_SpaceResources->ShowSpheres = true;
			else if (msg->Message == "hide spheres")
				g_SpaceResources->ShowSpheres = false;

			else if (msg->Message == "show debug")
				g_SpaceResources->ShowDebug = true;
			else if (msg->Message == "hide debug")
				g_SpaceResources->ShowDebug = false;
		}
	}

	bool pickNode(int x, int y, SpaceNode::ID* id)
	{
		Ray ray = m_Camera.createRay(x, y);

		Intersection::Hit hit;
		bool found = false;
		glm::vec3 position;

		float min_distance = std::numeric_limits<float>::max();

		for (unsigned int i = 0; i < m_SpaceNodes.size(); i++)
		{
			if (m_SpaceNodes[i] == NULL)
				continue;

			SpaceNode* spaceNode = static_cast<SpaceNode*>(m_SpaceNodes[i]);

            if (spaceNode->getLOD() == 0.0)
                continue;

            position = m_SpaceNodes[i]->getPosition();

			float pickRadius = g_SpaceResources->NodeIconSize * spaceNode->getSize() / 2.0f;

			if (Intersection::RaySphere(ray, position, pickRadius, hit))
			{
				if (id == NULL)
					return true;

				if (hit.distance < min_distance)
				{
					*id = i;
					min_distance = hit.distance;
				}

				found = true;
			}
		}

		return found;
	}

	void idle()
	{
		updateNodes();
		updateLinks();
		updateSpheres();

		// NOTE : We use the octree when we are not running the physics.
		// In the future, we will dynamically update the octree as the objects move inside it.
		if (m_PhysicsMode == PAUSE)
		{
		    updateOctree();
		}
		else
		{
		    SAFE_DELETE(m_Octree);
		}

		if (m_CameraAnimation)
		{
			float time = context()->sequencer().track("animation")->clock().seconds();
			glm::vec3 pos;
			float radius = m_DustAttractor.getRadius() * (0.4 + 0.25 * cos(time / 30.f));
			pos.x = radius * cos(time / 10.0f);
			pos.y = radius * cos(time / 50.0f);
			pos.z = radius * sin(time / 10.0f);
			m_Camera.lookAt(pos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0));
		}
	}

	void updateNodes()
	{
		const unsigned int c_MaxIterations = 100;
		bool c_Loop = true;

		if (m_Iterations < 1 || (!c_Loop  && m_Iterations > c_MaxIterations) || m_PhysicsMode == PAUSE)
			return;
		else if (m_Iterations == 1)
			m_LastUpdateTime = m_Clock.milliseconds();

		// m_SpaceNodes.setSpeed(20.0f / (float)m_Iterations);
		m_SpaceNodes.setSpeed(m_Temperature);

		// Reset node directions
		for (auto itn : m_SpaceNodes)
			if (itn != NULL)
				itn->setDirection(glm::vec3(0, 0, 0), false);

		// Calculate graph forces
		m_NodeRepulsionForce.apply(m_SpaceNodes);
		m_LinkAttractionForce.apply(m_SpaceNodes);
		m_DustAttractor.apply(m_SpaceNodes);
		// m_GravitationForce.apply(m_SpaceNodes);

		m_SpaceNodes.normalizeDirections();
		m_SpaceNodes.randomizeDirections();
		m_SpaceNodes.update();

		if (!c_Loop)
		{
			Timecode mean = (m_Clock.milliseconds() - m_LastUpdateTime) / m_Iterations;
			LOG("[SPACE] Iteration %u/%u, Mean Update Time : %lu ms, Estimated Remaining Time : %lu ms\n", m_Iterations, c_MaxIterations, mean, (c_MaxIterations - m_Iterations) * mean);
		}

		m_Iterations++;
		m_DirtyOctree = true;
	}

    void updateLinks()
    {
        if (g_SpaceResources->ShowEdges || g_SpaceResources->ShowEdgeActivity)
        {
            for (auto it : m_SpaceEdges)
                if (it != NULL)
                    static_cast<SpaceEdge*>(it)->update();
        }
    }

    void updateSpheres()
    {
        glm::vec3 position;
        float radius;

        std::vector<Sphere>::iterator its;
        for (its = model()->spheres_begin(); its != model()->spheres_end(); ++its)
        {
            if (m_SpaceSpheres[its->id()] != NULL)
            {
                computeBoundingSphere(*its, &position, &radius);
                m_SpaceSpheres[its->id()]->setPosition(position);
                static_cast<SpaceSphere*>(m_SpaceSpheres[its->id()])->setRadius(radius);
            }
        }
    }

    void updateOctree()
    {
        if (!m_DirtyOctree)
            return;

        if (g_SpaceResources->ShowDebug)
        {
            LOG("[SPACE] Updating octree ...\n");
            LOG("[SPACE] . Calculating bounding box ...\n");
        }

        glm::vec3 min;
        glm::vec3 max;

        unsigned long count = 0;

        for (unsigned long i = 0; i < m_SpaceNodes.size(); i++)
        {
            if (m_SpaceNodes[i] == NULL)
                continue;

            if (count == 0)
            {
                min = max = m_SpaceNodes[i]->getPosition();
                count++;
                continue;
            }

            glm::vec3 pos = m_SpaceNodes[i]->getPosition();

            for (int i = 0; i < 3; i++)
                min[i] = pos[i] < min[i] ? pos[i] : min[i];
            for (int i = 0; i < 3; i++)
                max[i] = pos[i] > max[i] ? pos[i] : max[i];

            count++;
        }

        SAFE_DELETE(m_Octree);

        if (count == 0)
        {
            if (g_SpaceResources->ShowDebug)
            {
                LOG("[SPACE] . Graph is empty! Aborting.\n");
            }
            m_DirtyOctree = false;
            return;
        }

        if (g_SpaceResources->ShowDebug)
        {
            LOG("[SPACE]   . Min : (%f, %f, %f)\n", min.x, min.y, min.z);
            LOG("[SPACE]   . Max : (%f, %f, %f)\n", max.x, max.y, max.z);
        }

        m_Octree = new Octree(min, max);

        for (unsigned long i = 0; i < m_SpaceNodes.size(); i++)
        {
            if (m_SpaceNodes[i] != NULL)
                m_Octree->insert(m_SpaceNodes[i]);
        }
        for (unsigned long i = 0; i < m_SpaceEdges.size(); i++)
        {
            if (m_SpaceEdges[i] != NULL)
                m_Octree->insert(m_SpaceEdges[i]);
        }

        m_DirtyOctree = false;
    }

	inline Camera* camera() { return &m_Camera; }

    inline void setNodeSize(float size) { g_SpaceResources->NodeIconSize = size; }
    inline void setEdgeSize(float size) { g_SpaceResources->EdgeSize = size; }
	inline void setTemperature(float temperature) { LOG("Temperature : %f\n", temperature); m_Temperature = temperature; }

	void checkNodeUID(Node::ID uid)
	{
		if (!m_NodeMap.containsRemoteID(uid))
		{
			LOG("Node UID %lu not found !\n", uid);
			throw;
		}
	}
	void checkLinkUID(Link::ID uid)
	{
		if (!m_LinkMap.containsRemoteID(uid))
		{
			LOG("Link UID %lu not found !\n", uid);
			throw;
		}
	}

    // ----- Graph Events ----

    void onSetAttribute(const std::string& name, VariableType type, const std::string& value)
    {
        FloatVariable vfloat;
        Vec3Variable vvec3;
        StringVariable vstring;
        BooleanVariable vbool;

        if (name == "space:update" && type == BOOLEAN)
        {
            applyDegreeTint();
        }
        else if (name == "space:animation" && type == BOOLEAN)
        {
            vbool.set(value);
            m_CameraAnimation = vbool.value();
        }
        else if (name == "space:linkmode" && type == STRING)
        {
            vstring.set(value);
            if (value == "node_color")
                g_SpaceResources->m_LinkMode = SpaceResources::NODE_COLOR;
            else if (value == "link_color")
                g_SpaceResources->m_LinkMode = SpaceResources::LINK_COLOR;
        }
        else if (name == "space:debug" && type == BOOLEAN)
        {
            vbool.set(value);
            g_SpaceResources->ShowDebug = vbool.value();
        }
    }

	void onAddNode(Node::ID uid, const char* label)
	{
		pushNodeVertexAround(uid, label, glm::vec3(0, 0, 0), 2);
		m_DirtyOctree = true;
	}

	void onRemoveNode(Node::ID uid)
	{
		checkNodeUID(uid);

		SpaceNode::ID vid = m_NodeMap.getLocalID(uid);

		for (unsigned int i = 0; i < m_SpaceEdges.size(); i++)
		{
			if (m_SpaceEdges[i] != NULL)
			{
				SpaceEdge* link = static_cast<SpaceEdge*>(m_SpaceEdges[i]);

				if (link->getNode1() == vid || link->getNode2() == vid)
				{
					m_SpaceEdges.remove(i);
					m_LinkMap.removeLocalID(i);
				}
			}
		}

		// TODO : Remove node from spheres here

		m_SpaceNodes.remove(vid);
		m_NodeMap.eraseRemoteID(uid, vid);

		m_DirtyOctree = true;
	}

	void onSetNodeAttribute(Node::ID uid, const std::string& name, VariableType type, const std::string& value)
	{
		FloatVariable vfloat;
		BooleanVariable vbool;
		StringVariable vstring;
		Vec3Variable vvec3;
		Vec4Variable vvec4;
		IntVariable vint;

		checkNodeUID(uid);
		SpaceNode::ID id = m_NodeMap.getLocalID(uid);

		if (name == "space:locked" && type == BOOLEAN)
		{
			vbool.set(value);
			m_SpaceNodes[id]->setPositionLock(vbool.value());
		}
		else if ((name == "space:position" || name == "particles:position") && type == VEC3)
		{
			vvec3.set(value);
			m_SpaceNodes[id]->setPosition(vvec3.value());
			m_DirtyOctree = true;
		}
        else if (name == "space:color" && (type == VEC3 || type == VEC4))
        {
            glm::vec4 c;

            if (type == VEC3)
            {
                vvec3.set(value);
                c = glm::vec4(vvec3.value(), 1.0);
            }
            else
            {
                vvec4.set(value);
                c = vvec4.value();
            }
            static_cast<SpaceNode*>(m_SpaceNodes[id])->setColor(c);
        }
        else if (name == "space:lod" && type == FLOAT)
        {
            vfloat.set(value);
            static_cast<SpaceNode*>(m_SpaceNodes[id])->setLOD(vfloat.value());
        }
        else if (name == "space:activity" && type == FLOAT)
        {
            vfloat.set(value);
            static_cast<SpaceNode*>(m_SpaceNodes[id])->setActivity(vfloat.value());
        }
        else if (name == "space:icon" && type == STRING)
        {
            vstring.set(value);
            static_cast<SpaceNode*>(m_SpaceNodes[id])->setIcon(vstring.value());
        }
        else if (name == "space:mark" && type == INT)
        {
            vint.set(value);
            static_cast<SpaceNode*>(m_SpaceNodes[id])->setMark(vint.value());
        }
        else if (name == "space:size" && type == FLOAT)
        {
            vfloat.set(value);
            static_cast<SpaceNode*>(m_SpaceNodes[id])->setSize(vfloat.value());
        }
	}

	void onSetNodeLabel(Node::ID uid, const char* label)
	{
		checkNodeUID(uid);

		SpaceNode::ID id = m_NodeMap.getLocalID(uid);

		static_cast<SpaceNode*>(m_SpaceNodes[id])->setLabel(label);
	}

	void onTagNode(Node::ID node, Sphere::ID sphere)
	{
		(void) node;
		(void) sphere;
	}

	void onAddLink(Link::ID uid, Node::ID uid1, Node::ID uid2)
	{
		checkNodeUID(uid1);
		checkNodeUID(uid2);

		SpaceNode::ID node1 = m_NodeMap.getLocalID(uid1);
		SpaceNode::ID node2 = m_NodeMap.getLocalID(uid2);

		SpaceEdge::ID lid = m_SpaceEdges.add(new SpaceEdge(m_SpaceNodes[node1], m_SpaceNodes[node2]));

		m_LinkMap.addRemoteID(uid, lid);
		m_DirtyOctree = true;
	}

	void onRemoveLink(Link::ID uid)
	{
		checkLinkUID(uid);

		SpaceEdge::ID vid = m_LinkMap.getLocalID(uid);

		m_SpaceEdges.remove(vid);
		m_LinkMap.eraseRemoteID(uid, vid);

		m_DirtyOctree = true;
	}

	void onSetLinkAttribute(Link::ID uid, const std::string& name, VariableType type, const std::string& value)
	{
		checkLinkUID(uid);

		FloatVariable vfloat;
        Vec3Variable vvec3;
        Vec4Variable vvec4;
        StringVariable vstring;

		SpaceEdge::ID id = m_LinkMap.getLocalID(uid);

        if (name == "space:color" && (type == VEC3 || type == VEC4))
        {
            checkLinkUID(uid);
            SpaceEdge::ID id = m_LinkMap.getLocalID(uid);

            glm::vec4 c;
            if (type == VEC3)
            {
                vvec3.set(value);
                c = glm::vec4(vvec3.value(), 1.0);
            }
            else
            {
                vvec4.set(value);
                c = vvec4.value();
            }

            static_cast<SpaceEdge*>(m_SpaceEdges[id])->setColor(0, c);
            static_cast<SpaceEdge*>(m_SpaceEdges[id])->setColor(1, c);
            static_cast<SpaceEdge*>(m_SpaceEdges[id])->setDirty(true);
        }
		else if (name == "space:color1" && type == VEC4)
		{
			checkLinkUID(uid);
			SpaceEdge::ID id = m_LinkMap.getLocalID(uid);
			vvec4.set(value);
			static_cast<SpaceEdge*>(m_SpaceEdges[id])->setColor(0, vvec4.value());
            static_cast<SpaceEdge*>(m_SpaceEdges[id])->setDirty(true);
		}
		else if (name == "space:color2" && type == VEC4)
		{
			checkLinkUID(uid);
			SpaceEdge::ID id = m_LinkMap.getLocalID(uid);
			vvec4.set(value);
			static_cast<SpaceEdge*>(m_SpaceEdges[id])->setColor(1, vvec4.value());
            static_cast<SpaceEdge*>(m_SpaceEdges[id])->setDirty(true);
		}
		else if (name == "space:width" && type == FLOAT)
		{
		    vfloat.set(value);
		    static_cast<SpaceEdge*>(m_SpaceEdges[id])->setWidth(vfloat.value());
		}
        else if (name == "space:activity" && type == FLOAT)
        {
            vfloat.set(value);
            static_cast<SpaceEdge*>(m_SpaceEdges[id])->setActivity(vfloat.value());
        }
        else if (name == "space:lod" && type == FLOAT)
        {
            vfloat.set(value);
            static_cast<SpaceEdge*>(m_SpaceEdges[id])->setLOD(vfloat.value());
        }
        else if (name == "space:icon" && type == STRING)
        {
            vstring.set(value);
            static_cast<SpaceEdge*>(m_SpaceEdges[id])->setIcon(vstring.value());
        }
	}

	void onAddSphere(Sphere::ID id, const char* label)
	{
		(void) id;
		(void) label;
		m_SpaceSpheres.add(new SpaceSphere());
	}

	void onSetSphereMark(Sphere::ID id, unsigned int mark)
	{
		static_cast<SpaceSphere*>(m_SpaceSpheres[id])->setColor(MarkerWidget::color(mark));
	}

	void onAddNeighbor(const std::pair<Node::ID, Link::ID>& element, const char* label, Node::ID neighbor)
	{
		checkNodeUID(neighbor);

		SpaceNode::ID nid = m_NodeMap.getLocalID(neighbor);

		SpaceNode::ID vid = pushNodeVertexAround(element.first, label, m_SpaceNodes[nid]->getPosition(), 2);
		SpaceEdge::ID lid = m_SpaceEdges.add(new SpaceEdge(m_SpaceNodes[nid], m_SpaceNodes[vid]));

		m_LinkMap.addRemoteID(element.second, lid);

		m_DirtyOctree = true;
	}

	// -----

	inline Scene::NodeVector& getNodes() { return m_SpaceNodes; }
	inline Scene::NodeVector& getEdges() { return m_SpaceEdges; }
	inline Scene::NodeVector& getSpheres() { return m_SpaceSpheres; }

	inline NodeTranslationMap& getNodeMap() { return m_NodeMap; }
	inline LinkTranslationMap& getLinkMap() { return m_LinkMap; }

	inline GraphContext* context() { return static_cast<GraphContext*>(m_GraphEntity->context()); }
	inline GraphModel* model() { return static_cast<GraphModel*>(m_GraphEntity->model()); }

private:
	unsigned int m_WindowWidth;
	unsigned int m_WindowHeight;

	GraphEntity* m_GraphEntity;

	Clock m_Clock;
	Timecode m_LastUpdateTime;

	Camera m_Camera;
	bool m_CameraAnimation;

	NodeTranslationMap m_NodeMap;
	LinkTranslationMap m_LinkMap;

	Scene::NodeVector m_SpaceNodes;
	Scene::NodeVector m_SpaceEdges;
	Scene::NodeVector m_SpaceSpheres;

	Octree* m_Octree;
	bool m_DirtyOctree;

	PhysicsMode m_PhysicsMode;
	unsigned int m_Iterations;

	LinkAttractionForce m_LinkAttractionForce;
	NodeRepulsionForce m_NodeRepulsionForce;
	Physics::GravitationForce m_GravitationForce;
	DustAttractor m_DustAttractor;

	float m_Temperature;
};
