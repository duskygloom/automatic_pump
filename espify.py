import sys

def main():
    infile = "automatic_pump.html"
    outfile = "output.txt"
    if (len(sys.argv) > 1):
        infile = sys.argv[1]
    htmlfile = open(infile, "r")
    textfile = open(outfile, "w")
    line = htmlfile.readline()
    while (line != ""):
        textfile.write(f"client.println(\"{line.rstrip()}\");\n")
        line = htmlfile.readline()
    htmlfile.close()
    textfile.close()

if __name__ == "__main__":
    main()
