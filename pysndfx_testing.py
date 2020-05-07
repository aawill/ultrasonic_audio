from pysndfx import AudioEffectsChain
import librosa
import subprocess as sp
import shlex
from io import BufferedReader, BufferedWriter
import os

cd = os.path.dirname(os.path.realpath(__file__)) 

os.chdir(cd)
# prog = sp.Popen('set AUDIODRIVER=waveaudio',stdin=sp.PIPE, shell=True)
# prog.communicate()
# prog2 = sp.Popen(['sox', 'sine.wav', '-d'], stdin=sp.PIPE)
# prog2.communicate()

prog = sp.Popen('', stdin=sp.PIPE, stdout=sp.PIPE, stderr=sp.STDOUT)
stdout_data = prog.communicate(input='ls\n'.encode())[0]
#prog.stdin.write(b'echo hello world\n')
# print('results:', prog.stdout)
for line in stdout_data:
    if line == "END\n":
        break
    print(line.decode('utf-8'),end="")
# prog.communicate()

#print('results:', prog.stdout.read())

# prog2 = sp.Popen('echo two', stdin=sp.PIPE)
# #prog.stdin.write(b'230028aa')
# prog.communicate()



#cmd = [cd, 'sox', 'sine.wav', '-d']
# cmd = [cd, 'echo hello world']
# print('running command:', cmd)
# response = subprocess.Popen(cmd)
# print('response:', response.returncode)

# fx = (
#   AudioEffectsChain()
#   .phaser()
# )

# infile = 'sine.wav'
# outfile = 'processed_sine.wav'

# x, sr = librosa.load(infile)
# y = fx(x)

# librosa.output.write_wav(outfile, y, sr)


# fx(infile, outfile)
