import moderngl
import moderngl_window as mglw
from pyrr import Matrix44

import cv2
import numpy as np
import os
from array import array

from prediction import predict, get_camera_matrix, get_fov_y, solvepnp, reproject


class CameraAR(mglw.WindowConfig):
    gl_version = (3, 3)
    title = "CameraAR"
    resource_dir = os.path.normpath(os.path.join(__file__, '../data'))
    previousTime = 0
    currentTime = 0
    
    def __init__(self, **kwargs):
        super().__init__(**kwargs)

        # Shader for rendering 3D objects
        self.prog3d = self.ctx.program(
            vertex_shader='''
                #version 330

                uniform mat4 Mvp;

                in vec3 in_position;
                in vec3 in_normal;
                in vec2 in_texcoord_0;

                out vec3 v_vert;
                out vec3 v_norm;
                out vec2 v_text;

                void main() {
                    gl_Position = Mvp * vec4(in_position, 1.0);
                    v_vert = in_position;
                    v_norm = in_normal;
                    v_text = in_texcoord_0;
                }
            ''',
            fragment_shader='''
                #version 330

                uniform vec3 Color;
                uniform vec3 Light;
                uniform sampler2D Texture;
                uniform bool withTexture;

                in vec3 v_vert;
                in vec3 v_norm;
                in vec2 v_text;

                out vec4 f_color;

                void main() {
                    float lum = clamp(dot(normalize(Light - v_vert), normalize(v_norm)), 0.0, 1.0) * 0.8 + 0.2;
                    if (withTexture) {
                        f_color = vec4(Color * texture(Texture, v_text).rgb * lum, 1.0);
                    } else {
                        f_color = vec4(Color * lum, 1.0);
                    }
                }
            ''',
        )
        self.mvp = self.prog3d['Mvp']
        self.light = self.prog3d['Light']
        self.color = self.prog3d['Color']
        self.withTexture = self.prog3d['withTexture']

        # Load the 3D virtual object, and the marker for hand landmarks
        self.scene_cube = self.load_scene('crate.obj')
        self.scene_marker = self.load_scene('marker.obj')

        # Extract the VAOs from the scene
        self.vao_cube = self.scene_cube.root_nodes[0].mesh.vao.instance(self.prog3d)
        self.vao_marker = self.scene_marker.root_nodes[0].mesh.vao.instance(self.prog3d)

        # Texture of the cube
        self.texture = self.load_texture_2d('crate.png')
        
        # Define the initial position of the virtual object
        # The OpenGL camera is position at the origin, and look at the negative Z axis. The object is at 30 centimeters in front of the camera. 
        self.object_pos = np.array([0.0, 0.0, -30.0])
        
        
        """
        --------------------------------------------------------------------
        TODO: Task 3. 
        Add support to render a rectangle of window size. 
        --------------------------------------------------------------------
        """
        vertices = np.array([
            -1.0, -1.0, 0.0,    0.0, 0.0, 1.0,   0.0, 1.0,
             1.0, -1.0, 0.0,    0.0, 0.0, 1.0,   1.0, 1.0,
            -1.0,  1.0, 0.0,    0.0, 0.0, 1.0,   0.0, 0.0,
             1.0,  1.0, 0.0,    0.0, 0.0, 1.0,   1.0, 0.0,
        ], dtype='f4')

        self.vbo = self.ctx.buffer(vertices.tobytes())
        # fullscreen quad
        self.quad = self.ctx.simple_vertex_array(self.prog3d, self.vbo, 'in_position', 'in_normal', 'in_texcoord_0')        
        
        # Start OpenCV camera 
        self.capture = cv2.VideoCapture(0)
        
        # Get a frame to set the window size and aspect ratio
        ret, frame = self.capture.read() 
        print("[DEBUG] Camera read:", ret)
        self.aspect_ratio = float(frame.shape[1]) / frame.shape[0]
        print("[DEBUG] Aspect ratio set to:",  self.aspect_ratio)
        self.window_size = (int(720.0 * self.aspect_ratio), 720)
        print("[DEBUG] Window size set to:", self.window_size)

    def render(self, time: float, frame_time: float):
        self.ctx.clear(1.0, 1.0, 1.0)
        
        self.ctx.disable(moderngl.DEPTH_TEST | moderngl.CULL_FACE)

        """
        ---------------------------------------------------------------
        TODO: Task 3. 
        Get OpenCV video frame, display in OpenGL. 
        Render the frame to a screen-sized rectange. 
        ---------------------------------------------------------------
        """
        ret, frame = self.capture.read()
        if not ret:
            return
        # flip and convert to rgb
        frame = cv2.flip(frame, 1)
        frame = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        
        # upload as texture
        if not hasattr(self, "frame_tex"):
            self.frame_tex = self.ctx.texture(frame.shape[1::-1], 3)
        self.frame_tex.write(frame.tobytes())
        self.frame_tex.use()

        identity = Matrix44.identity()  # screen-space fullscreen
        self.prog3d['Mvp'].write(identity.astype('f4'))
        self.prog3d['Color'].value = (1.0, 1.0, 1.0)
        self.prog3d['Light'].value = (0.0, 0.0, 100.0)
        self.prog3d['withTexture'].value = True
        
        self.quad.render(moderngl.TRIANGLE_STRIP)
        
        self.ctx.enable(moderngl.DEPTH_TEST | moderngl.CULL_FACE)
        
        """
        ---------------------------------------------------------------
        TODO: Task 4.
        Perform hand landmark prediction, and 
        solve PnP to get world landmarks list.
        ---------------------------------------------------------------
        """
        
        # Solve the landmarks in world space
        world_landmarks_list = []
        converted_world_landmarks = []
        
        # OpenCV to OpenGL conversion
        # The world points from OpenCV need some changes to be OpenGL ready. 
        # First, the model points are in meters (MediaPipe convention), while our camera matrix is in units. There exists a scale ambiguity of the true hand landmarks, i.e., if we scale up the world points by 1000, its projection remains the same (due to perspective division). 
        # Here we shift the measurement from meter to centimeter, and assume our world space in OpenGL is in centimeters, just for easy visualization and object interaction. So we multiply all points by 100.
        
        # Second, the OpenCV and OpenGL camera coordinate system are different. # OpenCV: right x, down y, into screen z. Image: right x, down y.  
        # OpenGL: right x, up y, out of screen z. Image: right x, up y.
        # Check for image and 3D points flip to make sure the points are properly converted. 

        # Get frame dimensions for camera matrix
        frame_height, frame_width = frame.shape[:2]
        camera_matrix = get_camera_matrix(frame_width, frame_height)

        # Hand detection
        detection_result = predict(frame)
        if detection_result and detection_result.hand_landmarks and len(detection_result.hand_landmarks) > 0:
            model_landmarks_list = detection_result.hand_world_landmarks
            image_landmarks_list = detection_result.hand_landmarks
            world_landmarks_list = solvepnp(model_landmarks_list, 
                image_landmarks_list, camera_matrix, frame_width, frame_height)

            # convert OpenCV to OpenGL coordinates
            for hand_landmarks in world_landmarks_list:
                pts = np.array(hand_landmarks)
                pts *= 100 # m to cm
                pts[:,1] *= -1  # flip Y axis
                pts[:,2] *= -1 # flip Z axis
                converted_world_landmarks.append(pts)
        
        """
        ----------------------------------------------------------------------
        TODO: Task 5.
        We detect a simple pinch gesture, and check if the index finger hits 
        the cube. We approximate by just checking the finger tip is close 
        enough to the cube location.
        ----------------------------------------------------------------------
        """
        grabbed = False
        # It is recommended to work on this task last after all landmarks are in place.
        if detection_result and detection_result.hand_landmarks:
            for hand_landmarks in converted_world_landmarks:
                thumb_tip = hand_landmarks[4]
                index_tip = hand_landmarks[8]
                # pinch detection
                pinch_distance = np.linalg.norm(index_tip - thumb_tip)
                is_pinch = pinch_distance < 3.0 
                # cube hit detection
                cube_to_tip = np.linalg.norm(index_tip - self.object_pos)
                is_hit = cube_to_tip < 4.0

                grabbed = is_pinch and is_hit
                if grabbed:
                    self.object_pos = index_tip.copy()
                    print(f"[DEBUG] Grabbed! Pinch={pinch_distance:.2f}cm  Dist={cube_to_tip:.2f}cm")
                    break
        
        """
        ----------------------------------------------------------------------
        TODO: Task 4. 
        Render the markers.
        ----------------------------------------------------------------------
        """
        # Note we have to set the OpenGL projection matrix by following parameters from the OpenCV camera matrix, i.e., the field of view.
        # You can use Matrix44.perspective_projection function, and set the parameters accordingly. Note that the fov must be computed based on the camera matrix. See prediction.py. 
        
        fov_y = get_fov_y(camera_matrix, frame_height) # Use real camera FOV
        proj = Matrix44.perspective_projection(fov_y, self.aspect_ratio, 0.1, 1000.0)
        
        # Translate the object to its position 
        translate = Matrix44.from_translation(self.object_pos)
        
        # Add a bit of random rotation just to be dynamic
        rotate = Matrix44.from_y_rotation(np.sin(time) * 0.5 + 0.2)
        
        # Scale the object up for easy viewing
        scale = Matrix44.from_scale((3, 3, 3))
        
        mvp = proj * translate * rotate * scale
        self.color.value = (1.0, 1.0, 1.0)
        if grabbed: # A bit of feedback when the object is grabbed
            self.color.value = (1.0, 0.0, 0.0)
        self.light.value = (10, 10, 10)
        self.mvp.write(mvp.astype('f4'))
        self.withTexture.value = True
        
        # Render the object
        self.texture.use()
        self.vao_cube.render()
        
        # Render the landmarks
        for hand_landmarks in converted_world_landmarks:
            for pos in hand_landmarks:
                scale = Matrix44.from_scale([0.3, 0.3, 0.3])
                translate = Matrix44.from_translation(pos)
                marker_mvp = proj * translate * scale
                
                self.color.value = (0.0, 1.0, 0.0)
                self.light.value = (10.0, 10.0, 10.0)
                self.mvp.write(marker_mvp.astype('f4'))
                self.withTexture.value = False
                
                self.vao_marker.render()

        # Render 2D landmarks for sanity check
        # if detection_result and detection_result.hand_landmarks:
        #     for hand_landmarks in image_landmarks_list:
        #         for l in hand_landmarks:
        #             x = (l.x * 2.0) - 1.0
        #             y = 1.0 - (l.y * 2.0)
        #             pos = np.array([x * 30, y * 30, -30])
        #             scale = Matrix44.from_scale([0.2, 0.2, 0.2])
        #             translate = Matrix44.from_translation(pos)
        #             marker_mvp = proj * translate * scale

        #             self.color.value = (1.0, 0.0, 0.0)
        #             self.withTexture.value = False
        #             self.mvp.write(marker_mvp.astype('f4'))

        #             self.vao_marker.render()

    def on_render(self, time, frame_time):
        # wrap render, cuz version 3.1.1 calls this instead
        self.render(time, frame_time)

    def close(self):
        if self.capture:
            self.capture.release()


if __name__ == '__main__':
    CameraAR.run()
