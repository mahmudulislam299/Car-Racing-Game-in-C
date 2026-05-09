# 3D Endless Car Racing Game in C

This is a university-level 3D endless car racing game written in pure C using OpenGL and GLUT/freeglut. The game runs in Code::Blocks on Windows and uses only C functions, structures, arrays, OpenGL primitives, keyboard input, file handling, and timer-based animation.

The goal of the project is to present a complete playable arcade racing game with sports cars, enemy traffic, collision detection, scoring, high score saving, day-night lighting, and multiple changing environments.

## Project Files

- `glut_3d_racing_game.c` - main source code for the 3D racing game.
- `GLUT_3D_RACING_README.md` - project setup, explanation, algorithm, and viva notes.
- `freeglut.dll` - required runtime DLL for freeglut on Windows.
- `glut_highscore.dat` - automatically created/updated high score file.
- `car_game.c` - older project file; not required for this GLUT 3D version.

## Main Features

- Pure C language project.
- 3D graphics using OpenGL and GLUT/freeglut.
- Endless racing gameplay.
- Sports-style player car with wheels, lights, body details, stripes, and spoiler.
- Multiple enemy sports cars with different colors and styles.
- Smooth lane movement and steering feel.
- Moving road lane dividers for speed effect.
- City, river, hilly, village, desert, and flower garden environments.
- Environment changes every `6.5` seconds.
- Gradual transition between environments.
- Day-night cycle with sunrise, morning, afternoon, sunset, night, and late-night phases.
- Night lighting for street lamps, windows, headlights, and glowing objects.
- Score, high score, speed, distance, time of day, and environment display.
- Start menu, instructions screen, pause screen, and game over screen.
- Fair collision system using smaller bounding boxes.
- High score saved to file.

## Controls

- Left Arrow: move left.
- Right Arrow: move right.
- Up Arrow: increase speed.
- Down Arrow: decrease speed.
- P: pause or resume.
- I: open instructions from menu.
- Enter: start game from menu or instructions screen.
- R: restart after game over.
- Esc: exit, or return from instructions to menu.

## Code::Blocks Setup on Windows

1. Install Code::Blocks with MinGW.
2. Install or copy freeglut files for MinGW.
3. Put GLUT header files in:
   - `MinGW\include\GL`
4. Put the GLUT library file in:
   - `MinGW\lib`
5. Keep `freeglut.dll` beside the generated `.exe` file.
6. Open Code::Blocks.
7. Create a new empty C project.
8. Add `glut_3d_racing_game.c` to the project.
9. Open `Project > Build options > Linker settings`.
10. Add these linker libraries:
   - `freeglut`
   - `opengl32`
   - `glu32`
   - `winmm`
11. If math linker errors appear, also add:
   - `m`
12. Build and run the project.

If your GLUT package uses old GLUT instead of freeglut, use `glut32` instead of `freeglut`.

## Command Line Build

If MinGW is available from the terminal, the game can be compiled with:

```sh
gcc glut_3d_racing_game.c -o glut_3d_racing_game.exe -lfreeglut -lopengl32 -lglu32 -lwinmm -lm
```

On this project setup, the tested Code::Blocks MinGW command is:

```sh
"C:\Program Files\CodeBlocks\MinGW\bin\gcc.exe" glut_3d_racing_game.c -o glut_3d_racing_game.exe -lfreeglut -lopengl32 -lglu32 -lwinmm
```

## Game Algorithm

1. Start program.
2. Load high score from `glut_highscore.dat`.
3. Initialize OpenGL, lights, camera, and environment objects.
4. Show main menu.
5. When the player starts:
   - Reset score, distance, speed, player car, and enemies.
   - Spawn enemy cars ahead of the player.
6. Every frame:
   - Read keyboard input.
   - Smoothly move the player car toward the selected lane.
   - Move road markings and enemy cars toward the camera.
   - Update score and distance.
   - Increase difficulty slowly over time.
   - Update day-night cycle.
   - Select and blend the active environment.
   - Draw road, cars, buildings, trees, river, hills, desert, flowers, lamps, and UI.
   - Check collision between player and enemies.
7. If collision happens:
   - Change game state to Game Over.
   - Save high score if current score is greater.
8. Player can restart or exit.

## Collision Detection

The game uses rectangular hitboxes on the X-Z road plane.

Each car has:

- `x` position for left/right road location.
- `z` position for forward/backward distance.
- `width` for car body width.
- `depth` for car body length.

The function `getHitBox()` creates a smaller collision box than the visible model. This is important because decorative parts like spoilers, lights, wheels, and body curves should not cause unfair crashes.

The function `collide()` checks:

- Horizontal overlap on the X axis.
- Forward/backward overlap on the Z axis.
- Both cars must be active.

So the game ends only when the actual vehicle bodies overlap.

## Environment System

The current environment depends on `difficultyTimer`.

The environment order is:

1. City
2. River Area
3. Hilly Area
4. Village
5. Desert
6. Flower Garden

Each environment stays for `6.5` seconds. Near the end of an environment, `environmentBlend()` creates a smooth transition into the next environment. The renderer uses `environmentWeight()` to decide how strongly each scene should appear.

## Day-Night System

The variable `dayNightTime` moves from `0.0` to `1.0` repeatedly.

Different helper functions use that value:

- `daylightAmount()` controls brightness.
- `nightAmount()` controls night lights and glow.
- `sunsetAmount()` adds warm sunrise/sunset color.
- `timeOfDayName()` shows the current time label in the HUD.

The sky, world light, headlights, building windows, street lamps, and glow effects change gradually instead of switching suddenly.

## Important Functions for Viva

- `main()` - starts the program and registers GLUT callbacks.
- `initOpenGL()` - enables depth test, lighting, and smooth shading.
- `display()` - main draw callback for each frame.
- `timer()` - requests regular redraws for animation.
- `updateGame()` - handles movement, speed, score, difficulty, enemies, and collision.
- `drawScene3D()` - draws the full 3D world.
- `setupCamera()` - sets the perspective chase camera.
- `drawRoad()` - draws road, lane lines, and road texture.
- `drawCarModel()` - draws player and enemy sports cars.
- `drawEnvironment()` - draws all active scenery.
- `drawBuilding()` - draws modern city buildings and glowing windows.
- `drawRiverScene()` - draws the river-area environment.
- `drawHillScene()` - draws the hilly environment.
- `drawDesertScene()` - draws the desert environment.
- `drawFlowerGardenScene()` - draws the flower garden environment.
- `spawnEnemy()` - creates enemy cars in random safe lanes.
- `laneIsSafe()` - prevents enemies from spawning too close together.
- `getHitBox()` - creates fair collision boxes.
- `collide()` - checks if cars physically overlap.
- `normalKey()` - handles Enter, P, I, R, and Esc.
- `specialDown()` and `specialUp()` - handle arrow key movement.

## Code Structure

The source file is organized into these major parts:

1. Header comment and library includes.
2. Constants for window, road, states, and environments.
3. Structures for vehicles, scenery objects, and hitboxes.
4. Global variables for game state and objects.
5. Utility math and drawing helpers.
6. Lighting and sky color functions.
7. Vehicle drawing functions.
8. Road and environment drawing functions.
9. Enemy spawning and collision functions.
10. Gameplay update function.
11. HUD and menu drawing functions.
12. GLUT input/display/timer callbacks.
13. `main()` function.

## Why This Project Is Good for University Presentation

This project demonstrates:

- Modular programming in C.
- Structures and arrays.
- Real-time animation.
- OpenGL 3D rendering.
- Keyboard input handling.
- File handling for high score.
- Game states and menus.
- Collision detection.
- Day-night lighting.
- Environment transition logic.
- Beginner-friendly graphics built from simple primitives.

The code is intentionally written in a clear style so every part can be explained during viva.
