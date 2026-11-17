# OB_OPTIMIZE=js-layout (the default): both of the others at once. The
# engine is built for size (-Os) except JavaScriptCore and its interpreter
# (ob-optimize-js.cmake) and WebCore's style, layout and DOM code: css/,
# dom/, layout/, rendering/ and style/ (ob-optimize-layout.cmake), which get
# -O2. BBC News spent about 16.6 s of a load in style updates on a 68040
# (obcore-view -profile, 17 November 2026), against about 1.7 s in
# JavaScript.
include("${CMAKE_CURRENT_LIST_DIR}/ob-optimize-js.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/ob-optimize-layout.cmake")
