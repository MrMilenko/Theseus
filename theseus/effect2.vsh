vs.1.0

// v0  -- position
// v3  -- normal
// v7  -- tex coord
//
// c0-3   -- transpose world/view/proj matrix
// c5-8   -- inverse world/view matrix
// c10-13 -- transpose world/view matrix
// c15    -- front color
// c16    -- sideColor - frontColor

// transform position to screen in oPos
// transform normal to world space in r0
// transform position to world space in r3
dp4 oPos.x, v0, c0
dp4 r2.x, v0, c10
dp3 r0.x, v3, c5
dp4 oPos.y, v0, c1
dp4 r2.y, v0, c11
dp3 r0.y, v3, c6
dp4 oPos.z, v0, c2
dp4 r2.z, v0, c12
dp3 r0.z, v3, c7
dp4 oPos.w, v0, c3
dp4 r2.w, v0, c13

// normalize vectors
dp3 r0.w, r0, r0
dp3 r2.w, r2, r2	
rsq r1.w, r0.w
rsq r3.w, r2.w
mul r0, r0, r1.w
mul r2, r2, r3.w


// here we have r0 (normal) and r2 (position); nothing else is used...


// compute fall-off effect colors; D = normal dot position
dp3 r0.w, r0, r2
mov r1, c16
max r0.w, r0.w, -r0.w
mad oD0, r1, r0.w, c15


// Textures...
mov oT0, v7
