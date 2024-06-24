cd ../godot
# Create output_header_mapping.json file
python ../godot-speech/thirdparty/godot-cpp/compat_generator.py
cp output_header_mapping.json ../godot-speech/thirdparty/godot-cpp/output_header_mapping_godot.json
cd ../godot-speech/thirdparty/godot-cpp
scons
cd ../..
