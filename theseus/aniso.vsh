vs.1.0

; v0  -- position
; v3  -- normal
;
; c0-3   -- world/view/proj matrix
; c4     -- light vector
; c5-8   -- inverse/transpose world matrix
; c9     -- {0.0, 0.5, 1.0, -1.0}
; c10    -- eye point
; c11-14 -- world matrix

;transform position
dp4 oPos.x, v0, c0
dp4 oPos.y, v0, c1
dp4 oPos.z, v0, c2
dp4 oPos.w, v0, c3

;transform normal
dp3 r0.x, v3, c5
dp3 r0.y, v3, c6
dp3 r0.z, v3, c7

;normalize normal
dp3 r0.w, r0, r0
rsq r0.w, r0.w
mul r0, r0, r0.w

//;compute world space position
//dp4 r1.x, v0, c11
//dp4 r1.y, v0, c12
//dp4 r1.z, v0, c13
//dp4 r1.w, v0, c14

//;vector from point to eye
//add r2, c10, -r1

//;normalize e
//dp3 r2.w, r2, r2
//rsq r2.w, r2.w
//mul r2, r2, r2.w

//;h = Normalize( l + e )
//add r1, r2, c4

//;normalize h
//dp3 r1.w, r1, r1
//rsq r1.w, r1.w
//mul r1, r1, r1.w

// compute fall-off effect colors; D = normal dot position
dp3 r0.w, r0, c4
mov r1, c16
max r0.w, r0.w, -r0.w
mad oD0, r1, r0.w, c15

