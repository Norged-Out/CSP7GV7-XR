import cv2
import time
import mediapipe as mp
import argparse # easier to handle everything from command line

# handle args
parser = argparse.ArgumentParser(description="MediaPipe FaceMesh demo")

parser.add_argument("--webcam", action="store_true", help="Use webcam instead of video file")
parser.add_argument("--video", type=str, help="Input video file")
parser.add_argument("--save", type=str, help="Output annotated video file")
parser.add_argument("--no-annotate", action="store_true", help="Disable face mesh")
parser.add_argument("--faces", type=int, default=5, help="Maximum number of faces to detect")

args = parser.parse_args()

USE_WEBCAM = args.webcam
INPUT_VIDEO = args.video
SAVE_OUTPUT = args.save
ANNOTATE = not args.no_annotate
MAX_FACES = args.faces


# mediapipe setup
mp_drawing = mp.solutions.drawing_utils
mp_face_mesh = mp.solutions.face_mesh

drawingSpec = mp_drawing.DrawingSpec(
    color=(255, 255, 255),
    thickness=1,
    circle_radius=2
)

face_mesh = mp_face_mesh.FaceMesh(
    max_num_faces=MAX_FACES,
    refine_landmarks=True,
    min_detection_confidence=0.5,
    min_tracking_confidence=0.5
)
print("FaceMesh ready.")


# prepare video capture
if USE_WEBCAM:
    print("Opening webcam...")
    cap = cv2.VideoCapture(0)
elif INPUT_VIDEO:
    print(f"Opening video file: {INPUT_VIDEO}")
    cap = cv2.VideoCapture(INPUT_VIDEO)
else:
    print("Error: specify --webcam or --video <file>")
    exit()

if not cap.isOpened():
    print("Error: could not open video source.")
    exit()

print("Video source opened.")


# video writer
out = None

if SAVE_OUTPUT:
    fourcc = cv2.VideoWriter_fourcc(*'mp4v')
    fps_out = cap.get(cv2.CAP_PROP_FPS) or 30
    # match the resized frame dimensions
    orig_width = cap.get(cv2.CAP_PROP_FRAME_WIDTH)
    orig_height = cap.get(cv2.CAP_PROP_FRAME_HEIGHT)
    aspect_ratio = orig_width / orig_height
    width = int(512 * aspect_ratio)
    height = 512
    out = cv2.VideoWriter(SAVE_OUTPUT, fourcc, fps_out, (width, height))
    print(f"Saving annotated output to: {SAVE_OUTPUT}")


# fps timer
currentTime = 0
previousTime = 0

print("Processing started (ESC to quit)")
# As long as device is ready
while cap.isOpened():
    # Read the video frame
    success, image = cap.read()
    if not success:
        print("End of video or frame read failed")
        break
 
    # flip the image for a front-facing webcam view
    if USE_WEBCAM:
        image = cv2.flip(image, 1)

    # resizing the frame
    aspect_ratio = image.shape[1] / image.shape[0]
    image = cv2.resize(image, (int(512 * aspect_ratio), 512))

    # calculating the FPS
    currentTime = time.time()
    fps = 1 / (currentTime - previousTime)
    previousTime = currentTime

    # displaying FPS on the image
    cv2.putText(image, str(int(fps))+" FPS", (10, 70), 
                cv2.FONT_HERSHEY_COMPLEX, 1, (0, 255, 0), 2)
    
    # mediapipe processing
    results = face_mesh.process(image)
    if ANNOTATE and results.multi_face_landmarks:
        for face_landmarks in results.multi_face_landmarks:
            mp_drawing.draw_landmarks(
            image=image,
            landmark_list=face_landmarks,
            connections=mp_face_mesh.FACEMESH_TESSELATION,
            landmark_drawing_spec=drawingSpec,
            connection_drawing_spec=drawingSpec)

    if out:
        out.write(image)

    # Display
    cv2.imshow('OpenCV camera', image)

    if cv2.waitKey(5) & 0xFF == 27:
        print("ESC pressed, exiting.")
        break

# Clean up after the loop
cap.release()
if out:
    out.release()
cv2.destroyAllWindows()