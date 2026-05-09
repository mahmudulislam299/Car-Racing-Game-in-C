# 3D Endless Car Racing Game in C with OpenGL and GLUT

This project is a complete mini arcade-style 3D car racing game written in pure C. It uses OpenGL and GLUT/freeglut, so it can run in Code::Blocks on Windows with MinGW.

## Files

- `glut_3d_racing_game.c` - complete source code for the new 3D racing game.
- `highscore.dat` - automatically created/updated high score file when the game runs.

The old `car_game.c` file is not required for this new GLUT project.

## Main Features

- 3D perspective highway with moving lane dividers.
- Player sports car with smooth lane movement and steering tilt.
- Multiple enemy sports cars with random lane spawning, colors, rims, lights, stripes, wings, and different body shapes.
- Accurate collision detection using smaller 3D hitboxes on the X and Z axes.
- Score, high score, speed level, and distance display.
- Difficulty increases over time by raising road speed and traffic density.
- Start menu, instructions screen, pause screen, and game over screen.
- Decorated city environment with modern tall buildings, glass towers, barriers, street lights, traffic lights, small billboards, neon signs, and river sections.
- Automatic environment cycle: City > Tunnel > Village > City.
- Smooth environment blending near the end of each section so scenery does not switch suddenly.
- Gradual day-night cycle with sunrise, morning, afternoon, sunset, night, and late-night lighting.
- Night effects: street lamp glow, building window lights, car headlights, darker road/sky colors, and neon signs.
- Tunnel effects: dark walls, roof lights, stronger headlights, and clear visibility.
- Village effects: fields, trees, houses, farms, and rivers.
- Cleaner scenery with fewer, smaller billboards placed farther from the road.
- Headlight, shadow, road texture, skyline haze, and speed-line effects.
- Arcade balance with restored speed/challenge, wider road space, smoother steering, and fairer hitboxes.

## Controls

- Left Arrow: move car left.
- Right Arrow: move car right.
- Up Arrow: increase speed.
- Down Arrow: decrease speed.
- P: pause or resume the game.
- Enter: start/restart game or open selected menu item.
- I: open instructions from the start menu.
- M: return to main menu.
- Esc: exit.

## Code::Blocks Setup on Windows

1. Install Code::Blocks with MinGW.
2. Install freeglut for MinGW if it is not already installed.
3. Copy the freeglut files to your MinGW folder:
   - Header: `freeglut.h` / `glut.h` inside `MinGW\include\GL`
   - Library: `libfreeglut.a` or `freeglut.lib` inside `MinGW\lib`
   - DLL: `freeglut.dll` beside your `.exe` file, or inside `C:\Windows\System32`
4. Open Code::Blocks.
5. Create a new C project or empty project.
6. Add `glut_3d_racing_game.c` to the project.
7. Open Project > Build options > Linker settings.
8. Add these linker libraries:
   - `freeglut`
   - `opengl32`
   - `glu32`
   - `winmm`
   - `m` if your compiler reports math linker errors for `fmodf` or `fabsf`
9. Open Search directories if needed:
   - Compiler path: your `MinGW\include`
   - Linker path: your `MinGW\lib`
10. Build and run.

If your installation uses old GLUT instead of freeglut, use `glut32` instead of `freeglut` in the linker settings.

## Command Line Build Example

From this project folder, with MinGW available in PATH:

```sh
gcc glut_3d_racing_game.c -o glut_3d_racing_game.exe -lfreeglut -lopengl32 -lglu32 -lwinmm -lm
```

Keep `freeglut.dll` in the same folder as the generated `.exe` if Windows cannot find the DLL.

## Game Algorithm

1. Start the program and load the previous high score from `highscore.dat`.
2. Show the start menu.
3. When the player starts the game, initialize player car, enemy cars, score, distance, and scenery.
4. Every timer frame:
   - Read keyboard input.
   - Smoothly move the player car toward the selected lane.
   - Move road markings, enemy cars, and roadside objects toward the camera.
   - Update the day-night cycle gradually.
   - Adjust sky color, light color, lamp glow, car headlights, and building windows from the current time of day.
   - Select the active environment from distance covered.
   - Blend from the current environment to the next environment near the end of each section.
   - Spawn enemy cars in random safe lanes.
   - Increase score and distance.
   - Slowly increase speed and enemy count for difficulty.
   - Check collision between the player car hitbox and enemy car hitboxes.
5. If collision happens, change to the game over screen and save the high score.
6. The player can restart or return to the menu.

## Collision Detection Explanation

The game uses rectangular hitboxes on the road plane. Each vehicle has:

- `x` position: left/right location on the road.
- `z` position: forward/backward depth on the road.
- `width`: usable car width.
- `depth`: usable car length.

The visual car model is slightly larger than the collision box. This makes collisions feel fair, because spoilers, lights, rims, and decorative curves do not trigger crashes. The function `collide()` checks if the player hitbox overlaps an enemy hitbox on both the X axis and Z axis, and it ignores inactive cars.

## Environment System Explanation

The environment is selected from the distance covered by the player. Every section has a fixed length:

1. City
2. Tunnel
3. Village
4. City again

Near the end of each section, `environmentBlend()` gradually increases from 0 to 1. The renderer uses this value to mix the current environment with the next environment. This gives a smoother transition and avoids sudden scene changes.

## Important Functions for Viva

- `main()` - initializes GLUT, OpenGL, high score, and callbacks.
- `display()` - draws the current screen.
- `updateGame()` - updates movement, score, difficulty, spawning, and collision.
- `drawCarModel()` - renders 3D cars using OpenGL primitives.
- `drawRoad()` - renders the highway and moving lane dividers.
- `drawEnvironment()` - renders roadside scenery.
- `applyWorldLighting()` and `setSkyColor()` - create the gradual day-night lighting system.
- `drawBuilding()`, `drawNeonSign()`, and `drawBillboard()` - create the decorated city environment.
- `currentEnvironment()`, `environmentBlend()`, and `environmentWeight()` - control smooth City/Tunnel/Village transitions.
- `drawTunnelScene()`, `drawVillageHouse()`, `drawFarmPatch()`, and `drawVillageRiver()` - render the new environment types.
- `spawnEnemy()` - creates new traffic cars in random lanes.
- `getHitBox()` and `collide()` - implement accurate collision detection.
- `keyboard()`, `specialDown()`, and `specialUp()` - handle player input.

## Notes for Presentation

This project demonstrates modular C programming with structures, arrays, functions, file handling, keyboard input, 3D rendering, animation, and game logic. The code is intentionally written in a beginner-friendly style so it can be explained clearly in a university viva.
