#include <glad/glad.h>
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

// Scene control 
static int numBoxes = 1;				// Debug: set numBoxes to 1.
std::vector<glm::mat4> boxTransforms;	// We represent the scene by a single box and a number of transforms for drawing the box at different locations.

// for part 4 animation
std::vector<glm::vec3> boxRotationAxes;
std::vector<float> boxRotationSpeeds;
static bool animateBoxes = false;

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
	boxRotationAxes.clear();
	boxRotationSpeeds.clear();
	if (numBoxes == 1) {
		// Use this for debugging
		glm::mat4 modelMatrix = glm::mat4(1.0f);
		modelMatrix = glm::translate(modelMatrix, glm::vec3(0, 0, 0));
		modelMatrix = glm::scale(modelMatrix, glm::vec3(16, 16, 16));
		boxTransforms.push_back(modelMatrix);
	} else {
		// Generate boxes based on random position, rotation, and scale. 
		// Store their transforms.
		for (int i = 0; i < numBoxes; ++i) {
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

			boxRotationAxes.push_back(axis);
			boxRotationSpeeds.push_back((0.2f + randomFloat()) * 0.8f); // radians/sec
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

	// Ensure we can capture the escape key being pressed below
	glfwSetInputMode(window, GLFW_STICKY_KEYS, GL_TRUE);
	glfwSetKeyCallback(window, key_callback);

	// Ensure we can capture mouse cursor movement 
	glfwSetCursorPosCallback(window, cursor_position_callback);

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
	glm::mat4 projectionMatrix = glm::perspective(glm::radians(FoV), (float)windowWidth / windowHeight, zNear, zFar);

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
			for (int i = 0; i < numBoxes; ++i) {
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
			for (int i = 0; i < numBoxes; ++i) {
				box.render(vpLeft, boxTransforms[i]);
			}

			// Right eye pass (cyan channel)
			glColorMask(GL_FALSE, GL_TRUE, GL_TRUE, GL_FALSE); // G and B only
			glClear(GL_DEPTH_BUFFER_BIT);
			// Draw the boxes for the right eye
			for (int i = 0; i < numBoxes; ++i) {
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

		// Rotate boxes for Part 4
		if (numBoxes > 1 && animateBoxes) {
			for (int i = 0; i < numBoxes; ++i) {
				boxTransforms[i] = 
					glm::rotate(boxTransforms[i], 
								boxRotationSpeeds[i] * deltaTime, 
								boxRotationAxes[i]);
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

	// for part 4 animation
	if (key == GLFW_KEY_A && action == GLFW_PRESS) {
		animateBoxes = !animateBoxes;
		std::cout << "Box animation: " << (animateBoxes ? "ON" : "OFF") << std::endl;
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
		numBoxes = 1;
		generateScene();
	}

	if (key == GLFW_KEY_0) {
		numBoxes = 100;
		generateScene();
	}

	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GL_TRUE);
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
	// Optionally, you can implement your own mouse support.
}
