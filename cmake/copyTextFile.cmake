# Script mode: cmake -DsourcePath=<path> -DoutputPath=<path> -P copyTextFile.cmake
# Writes the text file to outputPath with CRLF line endings, whatever the source's are.
file(READ "${sourcePath}" contents)
string(REPLACE "\r\n" "\n" contents "${contents}")
string(REPLACE "\n" "\r\n" contents "${contents}")
file(WRITE "${outputPath}" "${contents}")
