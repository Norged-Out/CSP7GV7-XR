#include <glad/glad.h> // switched from gl.h to glad.h
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <render/shader.h>
#include <render/texture.h>
#include <models/box.h>

#include <vector>
#include <iostream>
#define _USE_MATH_DEFINES
#include <math.h>

static GLFWwindow *window;
static int windowWidth = 1024; 
static int windowHeight = 768;

static void key_callback(GLFWwindow *window, int key, int scancode, int action, int mode);
static void cursor_position_callback(GLFWwindow* window, double xpos, double ypos);
static void framebuffer_size_callback(GLFWwindow* window, int width, int height);

// OpenGL camera view parameters
static glm::vec3 originalEyeCenter(0, 0, 100);

static glm::vec3 eyeCenter = originalEyeCenter;
static glm::vec3 lookat(0, 0, 0);
static glm::vec3 up(0, 1, 0);

static glm::float32 FoV = 45;
static glm::float32 zNear = 0.1f; 
static glm::float32 zFar = 1000.0f;

// View control 
static float viewAzimuth = M_PI / 2;
static float viewPolar = M_PI / 2;
static float viewDistance = 100.0f;
static bool rotating = false;
static glm::mat4 projectionMatrix;


// Scene control 
static int numBoxes = 1;				// Debug: set numBoxes to 1.
std::vector<glm::mat4> boxTransforms;	// We represent the scene by a single box and a number of transforms for drawing the box at different locations.

// for Part 4: Black Hole

static float bhInnerRadius = 8.0f;   // event horizon-ish
static float bhOuterRadius = 200.0f; // spawn ring-ish
static float bhMinRadius = 40.0f;  // initial min spawn radius
static float bhMaxHeight = 50.0f;  // vertical spread

static float bhBaseAngSpeed = 1.6f;  // base orbital speed
static float bhBaseFallSpeed = 6.0f; // base inward drift

// Per-particle state
static std::vector<float> bhAngle;
static std::vector<float> bhRadius;
static std::vector<float> bhAngSpeed;
static std::vector<float> bhFallSpeed;
static std::vector<float> bhHeight;
static std::vector<float> bhYSpeed;
static std::vector<float> bhSpinSpeed;
static std::vector<float> bhScale;
static std::vector<glm::vec3> bhSpinAxis;

// Anaglyph control 
static float ipd = 2.0f;				// Distance between left/right eye.
// After you implement the anaglyph, adjust the IPD value to control the red/cyan offsets and depth perception. 

enum AnaglyphMode {
	None,
	ToeIn, 
	Asymmetric, 
	AnaglyphModeCount,
};

static std::string strAnaglyphMode[] = {
	"None", 
	"Toe-in", 
	"Asymmetric view frustum", 
	"Invalid",
};

static AnaglyphMode anaglyphMode = AnaglyphMode::None;

enum SceneMode {
	Debug,
	RandomBoxes,
	BlackHole,
};

static SceneMode sceneMode = SceneMode::Debug;

// Helper functions 

static void nextAnaglyphMode() {
	anaglyphMode = (AnaglyphMode)(((int)anaglyphMode + 1) % (int)AnaglyphModeCount);
}

static int randomInt() {
	return rand();
}

static float randomFloat() {
	float r = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
	return r;
}

static glm::vec3 randomVec3() {
	return glm::vec3(randomFloat(), randomFloat(), randomFloat());
}

static void generateScene() {
	boxTransforms.clear();
	//boxRotationAxes.clear();
	//boxRotationSpeeds.clear();
	if (sceneMode == SceneMode::Debug) {
		// Use this for debugging
		glm::mat4 modelMatrix = glm::mat4(1.0f);
		modelMatrix = glm::translate(modelMatrix, glm::vec3(0, 0, 0));
		modelMatrix = glm::scale(modelMatrix, glm::vec3(16, 16, 16));
		boxTransforms.push_back(modelMatrix);
	} else if (sceneMode == SceneMode::RandomBoxes) {
		// Generate boxes based on random position, rotation, and scale. 
		// Store their transforms.
		for (int i = 0; i < 100; ++i) {
			glm::vec3 position = 100.0f * (randomVec3() - 0.5f);
			float s = (1 + (randomInt() % 4)) * 1.0f;
			glm::vec3 scale(s, s, s);
			float angle = randomFloat() * M_PI * 2;
			glm::vec3 axis = glm::normalize(randomVec3() - 0.5f);

			glm::mat4 modelMatrix = glm::mat4(1.0f);
			modelMatrix = glm::translate(modelMatrix, position);
			modelMatrix = glm::rotate(modelMatrix, angle, axis);
			modelMatrix = glm::scale(modelMatrix, scale);
			boxTransforms.push_back(modelMatrix);
		}
	}
	else if (sceneMode == SceneMode::BlackHole) {
		int particleCount = 100;
		boxTransforms.resize(particleCount + 1);

		// Resize particle state arrays
		bhAngle.resize(particleCount);
		bhRadius.resize(particleCount);
		bhAngSpeed.resize(particleCount);
		bhFallSpeed.resize(particleCount);
		bhHeight.resize(particleCount);
		bhYSpeed.resize(particleCount);
		bhSpinSpeed.resize(particleCount);
		bhScale.resize(particleCount);
		bhSpinAxis.resize(particleCount);

		// Black hole cube at origin (index 0)
		{
			glm::mat4 modelMatrix(1.0f);
			modelMatrix = glm::translate(modelMatrix, glm::vec3(0, 0, 0));
			modelMatrix = glm::scale(modelMatrix, glm::vec3(15, 15, 15));
			boxTransforms[0] = modelMatrix;
		}

		for (int i = 0; i < particleCount; ++i) {
			float angle = randomFloat() * (float)(2.0 * M_PI);

			// spawn radius (biased outward)
			float radius = bhMinRadius + (bhOuterRadius - bhMinRadius) * (0.35f + 0.65f * randomFloat());
			float height = (randomFloat() * 2.0f - 1.0f) * bhMaxHeight;

			// faster nearer center
			float chaos = 0.4f + 1.6f * randomFloat();
			float angSpd = bhBaseAngSpeed * sqrt(bhOuterRadius / radius);
			float fallSpd = bhBaseFallSpeed * (0.35f + 0.65f * randomFloat());
			float ySpd = (randomFloat() * 2.0f - 1.0f) * 2.0f;
			float scale = 0.8f + 2.5f * (radius / bhOuterRadius);
			float direction = (randomFloat() < 0.5f) ? -1.0f : 1.0f;

			bhAngle[i] = angle;
			bhRadius[i] = radius;
			bhHeight[i] = height;
			bhAngSpeed[i] = angSpd * chaos * direction;
			bhFallSpeed[i] = fallSpd * chaos;
			bhYSpeed[i] = ySpd;
			bhScale[i] = scale;

			bhSpinAxis[i] = glm::normalize(randomVec3() - 0.5f);
			bhSpinSpeed[i] = 0.8f + 2.5f * randomFloat();

			float x = cosf(angle) * radius;
			float z = sinf(angle) * radius;

			glm::mat4 modelMatrix(1.0f);
			modelMatrix = glm::translate(modelMatrix, glm::vec3(x, height, z));
			modelMatrix = glm::rotate(modelMatrix, angle, glm::vec3(0, 1, 0));
			modelMatrix = glm::rotate(modelMatrix, randomFloat() * (float)(2.0 * M_PI), bhSpinAxis[i]);
			modelMatrix = glm::scale(modelMatrix, glm::vec3(scale));
			boxTransforms[i + 1] = modelMatrix;
		}
	}

}

// Debugging functions 

static void printAnaglyphMode() {
	std::cout << "Anaglyph mode: " << strAnaglyphMode[(int)anaglyphMode] << std::endl;
}

static void printVec3(glm::vec3 v) {
	std::cout << v.x << " " << v.y << " " << v.z << std::endl;
}

static void printMat4(glm::mat4 m) {
	// Column major
	std::cout << m[0][0] << " " << m[1][0] << " " << m[2][0] << " " << m[3][0] << std::endl;
	std::cout << m[0][1] << " " << m[1][1] << " " << m[2][1] << " " << m[3][1] << std::endl;
	std::cout << m[0][2] << " " << m[1][2] << " " << m[2][2] << " " << m[3][2] << std::endl;
	std::cout << m[0][3] << " " << m[1][3] << " " << m[2][3] << " " << m[3][3] << std::endl;
}

int main(void)
{
	// Initialise GLFW
	if (!glfwInit())
	{
		std::cerr << "Failed to initialize GLFW." << std::endl;
		return -1;
	}

	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // For MacOS
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Open a window and create its OpenGL context
	window = glfwCreateWindow(windowWidth, windowHeight, "Anaglyph Rendering", NULL, NULL);
	if (window == NULL)
	{
		std::cerr << "Failed to open a GLFW window." << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync

	// Ensure we can capture the escape key being pressed below
	glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);
	glfwSetKeyCallback(window, key_callback);

	// Ensure we can capture mouse cursor movement 
	glfwSetCursorPosCallback(window, cursor_position_callback);

	// Allow window resizing
	glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

	// Load OpenGL functions, gladLoadGL returns the loaded version, 0 on error.
	int version = gladLoadGL();
	if (version == 0)
	{
		std::cerr << "Failed to initialize OpenGL context." << std::endl;
		return -1;
	}

	srand(2024);

	// Background
	glClearColor(163 / 255.0f, 227 / 255.0f, 255 / 255.0f, 1.0f);
	
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	// Create a box
	Box box;
	box.initialize();

	// Create the scene with a set of boxes represented by their transforms
	generateScene();

	// Set a perspective camera 
	projectionMatrix = glm::perspective(glm::radians(FoV), (float)windowWidth / windowHeight, zNear, zFar);

	printAnaglyphMode();

	do
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Render anaglyph 

		if (anaglyphMode == None) {
			// Clear the screen
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

			// Set camera view matrix 
			glm::mat4 viewMatrix = glm::lookAt(eyeCenter, lookat, up);
			glm::mat4 vp = projectionMatrix * viewMatrix;
			
			// Draw 
			for (int i = 0; i < boxTransforms.size(); ++i) {
				box.render(vp, boxTransforms[i]);
			}

		} else {
			// Declare left and right eye view-projection matrices
			glm::mat4 vpLeft;
			glm::mat4 vpRight;

			if (anaglyphMode == ToeIn) {
				// Toe-in projection here

				// Left eye: offset the eye position to the left by ipd/2
				glm::vec3 eyeLeft = eyeCenter - glm::vec3(ipd / 2.0f, 0.0f, 0.0f);
				glm::mat4 viewMatrixLeft = glm::lookAt(eyeLeft, lookat, up);
				vpLeft = projectionMatrix * viewMatrixLeft;

				// Right eye: offset the eye position to the right by ipd/2
				glm::vec3 eyeRight = eyeCenter + glm::vec3(ipd / 2.0f, 0.0f, 0.0f);
				glm::mat4 viewMatrixRight = glm::lookAt(eyeRight, lookat, up);
				vpRight = projectionMatrix * viewMatrixRight;

			} else if (anaglyphMode == Asymmetric) {
				// Asymmetric view frustum here
				
				glm::vec3 forward = glm::normalize(lookat - eyeCenter);
				glm::vec3 right = glm::normalize(glm::cross(forward, up));

				// Left eye orientation
				glm::vec3 eyeLeft = eyeCenter - right * (ipd / 2.0f);
				glm::mat4 viewMatrixLeft = glm::lookAt(eyeLeft, eyeLeft + forward, up);

				// Right eye orientation
				glm::vec3 eyeRight = eyeCenter + right * (ipd / 2.0f);
				glm::mat4 viewMatrixRight = glm::lookAt(eyeRight, eyeRight + forward, up);

				// Symmetric frustum parameters
				float aspect = (float)windowWidth / (float)windowHeight;
				float top = zNear * tanf(glm::radians(FoV) / 2.0f);
				float bottom = -top;

				// Calculate frustum shift
				float shift = (ipd / 2.0f) * zNear / glm::length(lookat - eyeCenter);

				// Left eye frustum
				float leftL = -aspect * top + shift; // - near * (w - ipd) / 2d
				float rightL = aspect * top + shift; // near * (w + ipd) / 2d
				glm::mat4 projectionMatrixLeft = glm::frustum(leftL, rightL, bottom, top, zNear, zFar);
				vpLeft = projectionMatrixLeft * viewMatrixLeft;

				// Right eye frustum
				float leftR = -aspect * top - shift; // - near * (w + ipd) / 2d
				float rightR = aspect * top - shift; // near * (w - ipd) / 2d
				glm::mat4 projectionMatrixRight = glm::frustum(leftR, rightR, bottom, top, zNear, zFar);
				vpRight = projectionMatrixRight * viewMatrixRight;
			}

			// Two-pass rendering to draw the anaglyph

			glClear(GL_COLOR_BUFFER_BIT); // Clear all color channels

			// Left eye pass (red channel)
			glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE); // R only
			glClear(GL_DEPTH_BUFFER_BIT);
			// Draw the boxes for the left eye
			for (int i = 0; i < boxTransforms.size(); ++i) {
				box.render(vpLeft, boxTransforms[i]);
			}

			// Right eye pass (cyan channel)
			glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_FALSE); // G and B only
			glClear(GL_DEPTH_BUFFER_BIT);
			// Draw the boxes for the right eye
			for (int i = 0; i < boxTransforms.size(); ++i) {
				box.render(vpRight, boxTransforms[i]);
			}
			
			glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);  // Reset all channels

		}

		// --------------------------------------------------------------------


		// Animation
		static double lastTime = glfwGetTime();
		double currentTime = glfwGetTime();
		float deltaTime = float(currentTime - lastTime);
		lastTime = currentTime;
		if (rotating) {
			viewAzimuth += 1.0f * deltaTime;
			eyeCenter.x = viewDistance * cos(viewAzimuth);
			eyeCenter.z = viewDistance * sin(viewAzimuth);
		}
		
		// Black hole animation update
		if (sceneMode == SceneMode::BlackHole && boxTransforms.size() > 1) {
			int particleCount = (int)boxTransforms.size() - 1;

			// Rotate black hole cube slowly
			glm::mat4 modelMatrix(1.0f);
			modelMatrix = glm::rotate(modelMatrix, (float)currentTime * 0.3f, glm::vec3(1, 1, 1));
			modelMatrix = glm::scale(modelMatrix, glm::vec3(15, 15, 15));
			boxTransforms[0] = modelMatrix;

			for (int i = 0; i < particleCount; ++i) {
				// Orbital motion
				bhAngle[i] += bhAngSpeed[i] * deltaTime * (1.0f + 2.0f / std::max(bhRadius[i], 20.0f));
				// Vertical bobbing
				float wobble = sinf((float)currentTime * 0.7f + i) * 0.2f;
				bhAngle[i] += wobble * deltaTime;
				// Radial pull inward
				float pull = 1.0f + 40.0f / std::max(bhRadius[i], 20.0f);
				bhRadius[i] -= bhFallSpeed[i] * pull * deltaTime;

				// Vertical drift
				bhHeight[i] += bhYSpeed[i] * deltaTime;
				if (bhHeight[i] > bhMaxHeight) { bhHeight[i] = bhMaxHeight; bhYSpeed[i] *= -1.0f; }
				if (bhHeight[i] < -bhMaxHeight) { bhHeight[i] = -bhMaxHeight; bhYSpeed[i] *= -1.0f; }

				// Event horizon: respawn
				if (bhRadius[i] < bhInnerRadius) {
					// Reposition
					bhAngle[i] = randomFloat() * (float)(2.0 * M_PI);
					bhRadius[i] = bhMinRadius + (bhOuterRadius - bhMinRadius) * (0.4f + 0.3f * randomFloat());
					bhHeight[i] = (randomFloat() * 2.0f - 1.0f) * bhMaxHeight;
					float chaos = 0.4f + 1.6f * randomFloat();

					// Re-roll orbital speeds
					float direction = (randomFloat() < 0.5f) ? -1.0f : 1.0f;
					bhAngSpeed[i] = bhBaseAngSpeed * sqrt(bhOuterRadius / bhRadius[i]) * chaos * direction;

					// Radial + vertical speeds
					bhFallSpeed[i] = bhBaseFallSpeed * (0.35f + 0.65f * randomFloat()) * chaos;
					bhYSpeed[i] = (randomFloat() * 2.0f - 1.0f) * 2.0f;

					// Visuals
					bhSpinAxis[i] = glm::normalize(randomVec3() - 0.5f);
					bhSpinSpeed[i] = 0.8f + 2.5f * randomFloat();
				}

				// Occasional energy injection
				if (randomFloat() < 0.2f * deltaTime) {
					bhRadius[i] *= 0.5f;
				}

				// Reposition the particle
				float x = cosf(bhAngle[i]) * bhRadius[i];
				float z = sinf(bhAngle[i]) * bhRadius[i];

				// Tidal stretching (increases toward center)
				float baseScale = 0.5f + 2.5f * (bhRadius[i] / bhOuterRadius);
				float t = glm::clamp(1.0f - (bhRadius[i] / bhOuterRadius), 0.0f, 1.0f);
				float sx = baseScale * (1.0f + t * 1.5f);
				float sy = baseScale * (1.0f - t * 0.5f);
				float sz = baseScale * (1.0f + t * 1.5f);

				// Update transform
				glm::mat4 modelMatrix(1.0f);
				modelMatrix = glm::translate(modelMatrix, glm::vec3(x, bhHeight[i], z));
				modelMatrix = glm::rotate(modelMatrix, bhAngle[i], glm::vec3(1, 1, 1));
				modelMatrix = glm::rotate(modelMatrix, (float)currentTime * bhSpinSpeed[i], bhSpinAxis[i]);
				modelMatrix = glm::scale(modelMatrix, glm::vec3(sx, sy, sz));
				boxTransforms[i + 1] = modelMatrix;
			}
		}


		// Swap buffers
		glfwSwapBuffers(window);
		glfwPollEvents();

	} // Check if the ESC key was pressed or the window was closed
	while (!glfwWindowShouldClose(window));

	// Clean up
	box.cleanup();

	// Close OpenGL window and terminate GLFW
	glfwTerminate();

	return 0;
}

// Is called whenever a key is pressed/released via GLFW
void key_callback(GLFWwindow *window, int key, int scancode, int action, int mode)
{
	if (key == GLFW_KEY_SPACE && action == GLFW_PRESS)
	{
		std::cout << "Space key is pressed." << std::endl;
		rotating = !rotating;
	}

	if (key == GLFW_KEY_R && action == GLFW_PRESS)
	{
		std::cout << "Reset." << std::endl;
		rotating = false;
		eyeCenter = originalEyeCenter;
		viewAzimuth = M_PI / 2;
		viewPolar = M_PI / 2;
	}

	if (key == GLFW_KEY_UP && (action == GLFW_REPEAT || action == GLFW_PRESS))
	{
		viewPolar -= 0.1f;
		eyeCenter.y = viewDistance * cos(viewPolar);
	}

	if (key == GLFW_KEY_DOWN && (action == GLFW_REPEAT || action == GLFW_PRESS))
	{
		viewPolar += 0.1f;
		eyeCenter.y = viewDistance * cos(viewPolar);
	}

	if (key == GLFW_KEY_LEFT && (action == GLFW_REPEAT || action == GLFW_PRESS))
	{
		viewAzimuth -= 0.1f;
		eyeCenter.x = viewDistance * cos(viewAzimuth);
		eyeCenter.z = viewDistance * sin(viewAzimuth);
	}

	if (key == GLFW_KEY_RIGHT && (action == GLFW_REPEAT || action == GLFW_PRESS))
	{
		viewAzimuth += 0.1f;
		eyeCenter.x = viewDistance * cos(viewAzimuth);
		eyeCenter.z = viewDistance * sin(viewAzimuth);
	}

	if (key == GLFW_KEY_M && action == GLFW_PRESS) {
        nextAnaglyphMode(); 
		printAnaglyphMode();
	}

	// Adjust the IPD value to match your actual viewing distance
	// Special case: IPD == 0 means no 3D effect.

	if (key == GLFW_KEY_COMMA) {
		ipd -= 0.1f;
		ipd = std::max(ipd, 0.0f);
		std::cout << "IPD: " << ipd << std::endl;
	}

	if (key == GLFW_KEY_PERIOD) {
		ipd += 0.1f;
		std::cout << "IPD: " << ipd << std::endl;
	}

	if (key == GLFW_KEY_1) {
		sceneMode = SceneMode::Debug;
		generateScene();
	}

	if (key == GLFW_KEY_0) {
		sceneMode = SceneMode::RandomBoxes;
		generateScene();
	}

	// for Part 4: Black Hole
	if (key == GLFW_KEY_A) {
		sceneMode = SceneMode::BlackHole;
		eyeCenter = glm::vec3(0, 0, 150);
		std::cout << "Black Hole mode activated" << std::endl;
		generateScene();
	}

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GL_TRUE);
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
	// Optionally, you can implement your own mouse support.
}

static void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
	if (height == 0) return; // avoid divide-by-zero
	windowWidth = width;
	windowHeight = height;
	glViewport(0, 0, width, height);
	projectionMatrix = glm::perspective(glm::radians(FoV), (float)width / (float)height, zNear,	zFar);
}