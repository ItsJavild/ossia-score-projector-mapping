"""Compile the production multi-window fragment shader for desktop backends."""
import pathlib
import subprocess
import sys
import tempfile

source = pathlib.Path(__file__).resolve().parents[2] / 'src/plugins/score-plugin-gfx/Gfx/Graph/MultiWindowNode.cpp'
shader = source.read_text().split('static const constexpr auto frag_shader = R"_(', 1)[1].split(')_";', 1)[0]
with tempfile.TemporaryDirectory(prefix='score-projector-shader-') as directory:
    fragment = pathlib.Path(directory) / 'multiwindow.frag'
    fragment.write_text(shader)
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else 'qsb', '--glsl', '330,410', '--hlsl', '50', '--msl', '12', '-o', str(fragment) + '.qsb', str(fragment)], check=True)
print('Production projector shader compiled for SPIR-V, GLSL, HLSL, and Metal')
