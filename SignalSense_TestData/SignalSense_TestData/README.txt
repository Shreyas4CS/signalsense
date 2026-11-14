SignalSense – TEST DATA
Designed & developed by CognoSpace
=====================================================================

WHAT'S INSIDE
  0_RoadPhoto/road_for_stop_line.jpg       a road photo to draw the stop line on
  1_LineJump/train/behind   (22 photos)    car waiting behind the line
  1_LineJump/train/crossed  (22 photos)    car over / past the line
  1_LineJump/train/empty    (22 photos)    no cars on the road
  1_LineJump/test/...        (6 each)      NOT for training – use with "Test a photo"
  2_Vehicles/train/Car, Bus, Ambulance (16 each)
  2_Vehicles/test/Car, Bus, Ambulance  (5 each)  NOT for training – use with "Test a photo"

The scenes are made on a computer: the blue car is your real toy car,
the others are drawn. They are for practising the steps.
For the best results in Live mode, also add photos from YOUR camera.

---------------------------------------------------------------------
STEP 1 · Stop line
  Training mode → Road line → "Upload photo" → choose
  0_RoadPhoto/road_for_stop_line.jpg → the line snaps onto the tape → "Save line"

STEP 2 · Line-jump AI
  Training mode → Line jump
  • "Waiting behind the line" group → Upload → select ALL photos in 1_LineJump/train/behind
  • "Crossed the line" group        → Upload → select ALL photos in 1_LineJump/train/crossed
  • "Road empty" group              → Upload → select ALL photos in 1_LineJump/train/empty

STEP 3 · Vehicles (optional)
  Training mode → Vehicles → add groups: Car, Bus, Ambulance
  • In each group card → "Upload photos" → select all photos in 2_Vehicles/train/<group>
  (Or: "Upload photos to the tray", tick them, then press the group name.)

STEP 4 · Train
  Training mode → Train AI → "Train AI now"  (try Neural Network AND KNN and compare)

STEP 5 · Test with photos the AI has never seen
  • Line jump step → "🔍 Test a photo" → select the photos in 1_LineJump/test/crossed
    → most should say "Crossed the line". Try the behind/ and empty/ folders too.
  • Vehicles step → "🔍 Test a photo" → select photos in 2_Vehicles/test/Bus  → should say "Bus"

Class activity idea: count how many test photos the AI got right
for KNN and for the Neural Network – which brain wins?
