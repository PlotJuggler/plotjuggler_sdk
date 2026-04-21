# Intro

The purpose of this module is to implement all the code that we need to display 2D data (images, video, markers), from different datasets or from streaming sources.

The detailed set of requirements and goals can be found in pj_media/docs/REQUIREMENTS.d
You MUST read this file at the beginning of every section and after compacting.

# Validation

Before any comit, run the tests and check that they all pass.

Make sure that all the markdown files in this folder are updated, if necessary.

Lesson learned should be saved too, in particular after long debugging sections where we struggle to find the correct solution.

# Test-driven verification

- Always think first about how a certain piece of software can be tested automatically, instead of asking the user to run it and report the results.
- If the user reports an issue, think first about how to reproduce the issue in the tests. Do not attempt to fix the issue, unless we were able to reproduce it.
