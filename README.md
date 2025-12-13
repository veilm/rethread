# rethread
qutebrowser code is too complex and takes too long to compile. the people need a
solution

## running
```
git clone --depth 1 https://github.com/veilm/rethread
cd rethread

python3 bootstrap.py # pulls a binary for the cef lib from Spotify yes
make -j$(nproc)
./out/Release/rethread --help
```
