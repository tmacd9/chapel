/*
 * File I/O Primer
 *
 * This is a simple example of using file I/O in Chapel.
 *
 * It initializes an array and writes its size and data to a file.  It
 * then opens the file, uses the size in the file to declare a new
 * domain and array, and reads in the array data.
 */

config var n = 9,                 // the problem size
           filename = "Arr.dat";  // the filename for writing/reading the array


proc main {
  const ADom = [1..n, 1..n];  // Create a domain of the specified problem size

  // Create and initialize an array of the specified size
  var A: [ADom] real = [(i,j) in ADom] i + j/10.0;
  
  // Write the problem size and array out to the specified filename
  writeSquareArray(n, A, filename);
  
  // Read an array in from the specified filename, storing in a new variable, B
  var B = readArray(filename);
  
  // Print out B as a debugging step
  writeln("B is:\n", B);
  
  //
  // verify that A and B contain the same values and print success or failure
  //
  const numErrors = + reduce [i in ADom] (A(i) != B(i));

  if (numErrors > 0) {
    writeln("FAILURE");
  } else {
    writeln("SUCCESS");
  }
}


//
// this procedure writes a square array out to a file
//
proc writeSquareArray(n, X, filename) {
  // Create and open an output file with the specified filename in write mode
  var outfile = open(filename, "w");
  var writer = outfile.writer();

  // Write the problem size in each dimension to the file
  writer.writeln(n, " ", n);

  // write out the array itself
  writer.write(X);

  // close the file
  writer.close();
  outfile.close();
}


//
// This procedure reads a new array out of a file and returns it
//
proc readArray(filename) {
   // Open an input file with the specified filename in read mode
  var infile = open(filename, "r");
  var reader = infile.reader();

  // Read the number of rows and columns in the array in from the file
  var m = reader.read(int), 
      n = reader.read(int);

  // Declare an array of the specified dimensions
  var X: [1..m, 1..n] real;

  //
  // Read in the array elements one by one (eventually, you should be
  // able to read in the array wholesale, but this isn't currently
  // supported.
  //
  for i in 1..m do
    for j in 1..n do
      reader.read(X(i,j));

  // Close the file
  reader.close();
  infile.close();

  // Return the array
  return X;
}


