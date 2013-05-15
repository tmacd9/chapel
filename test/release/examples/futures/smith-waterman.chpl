use Time;
use IO;
use Futures;
use CyclicDist;
use BlockCycDist;

config const tileWidth = 1;
config const tileHeight = 1;

class ArrayData {
  var down: [0..#tileWidth] int;
  var right: [0..#tileHeight] int;

  proc bottomRow(idx: int) var {
    return down(idx);
  }

  proc rightColumn(idx: int) var {
    return right(idx);
  }
}

class Tile {

  var f: future(bool);
  var border: bool;
  var ad: ArrayData;

  proc Tile() {
    this.border = false;
  }

  proc initializeData(): void {
    this.ad = new ArrayData();
  }

  proc setBorder(): void {
    this.border = true;
  }

  proc get(): void {
    if (!this.border) {
      this.f.get();
    }
  }

  proc bottomRow(idx: int) var {
    return ad.bottomRow(idx);
  }

  proc rightColumn(idx: int) var {
    return ad.rightColumn(idx);
  }

  proc diagonal: int {
    return ad.bottomRow(tileWidth - 1);
  }
}

proc isSupported(inputChar: string): bool {
  var toBeReturned = false;
  select inputChar {
    when "A" do toBeReturned = true;
    when "C" do toBeReturned = true;
    when "G" do toBeReturned = true;
    when "T" do toBeReturned = true;
  }
  return toBeReturned;
}

proc charMap(inputChar: string, idx: int): int(8) {
  var toBeReturned: int(8) = -1;
  select inputChar {
    when "_" do toBeReturned = 0;
    when "A" do toBeReturned = 1;
    when "C" do toBeReturned = 2;
    when "G" do toBeReturned = 3;
    when "T" do toBeReturned = 4;
  }
  if (toBeReturned == -1) {
    halt("Unsupported character in at index ", idx, " for input: ", inputChar);
  } else {
    // writeln(inputChar, " maps to ", toBeReturned);
  }
  return toBeReturned;
}

proc printMatrix(array: [] real): void {
  writeln(array);
}

const alignmentScoreMatrix: [0..#5, 0..#5] int = (
  ( -1, -1, -1, -1, -1),
  ( -1,  2, -4, -2, -4),
  ( -1, -4,  2, -4, -2),
  ( -1, -2, -4,  2, -4),
  ( -1, -4, -2, -4,  2)
);


proc getIntArrayFromString(line: string) {
  // initially store the results in an associative domain
  var myDom: domain(int);
  var myArray: [myDom] int(8);
  var myArraySize = 1;

  {
    var lineLength = line.length;
    for i in {1..#(lineLength - 1)} do {
      var loopChar = line.substring(i);
      myDom += myArraySize;
      myArray(myArraySize) = charMap(loopChar, i);
      myArraySize = myArraySize + 1;
    }
  }

  var resDom: domain(1) = {1..#(myArraySize - 1)};
  var resArray: [resDom] int(8);
  [i in myDom] resArray(i) = myArray(i);

  return resArray;
}

proc main(): void {

  writeln("Main: PARALLEL starts...");

  var A = getIntArrayFromString("ACACACTA");
  var B = getIntArrayFromString("AGCACACA");

  var tmWidth = A.numElements / tileWidth;
  var tmHeight = B.numElements / tileHeight;

  writeln("Main: Configuration Summary");
  writeln("  numLocales = ", numLocales);
  writeln("  tileHeight = ", tileHeight);
  writeln("    B.length = ", B.numElements);
  writeln("    tmHeight = ", tmHeight);
  writeln("  tileWidth = ", tileWidth);
  writeln("    A.length = ", A.numElements);
  writeln("    tmWidth = ", tmWidth);
  stdout.flush();

  writeln("Main: initializing copies of A and B"); stdout.flush();
  const startTimeCopies = getCurrentTime();

  const distrSpace = {0..(numLocales - 1)};
  const distrDom: domain(1) dmapped Cyclic(startIdx=distrSpace.low) = distrSpace;

  var aDistr: [distrDom] [1..(A.numElements)] int(8);
  var bDistr: [distrDom] [1..(B.numElements)] int(8);

  [i in distrSpace] on Locales(i) do {
    writeln("Main: initializing data on locale-", i); stdout.flush();
    [j in {1..(A.numElements)}] aDistr(i)(j) = A(j);
    [j in {1..(B.numElements)}] bDistr(i)(j) = B(j);
  }

  const endTimeCopies = getCurrentTime();
  writeln("Main: initialized copies of A and B in ", (endTimeCopies - startTimeCopies), " seconds."); stdout.flush();

  const startTimeAlloc = getCurrentTime();

  var unmapped_tile_matrix_space = {0..tmHeight, 0..tmWidth};
  var mapped_tile_matrix_space = unmapped_tile_matrix_space dmapped Cyclic(startIdx=unmapped_tile_matrix_space.low);
  var tileMatrix: [mapped_tile_matrix_space] Tile;
  [ij in tileMatrix] on ij.locale do { ij = new Tile(); }

  const endTimeAlloc = getCurrentTime();
  writeln("Main: allocated tiles in ", (endTimeAlloc - startTimeAlloc), " seconds."); stdout.flush();

  on tileMatrix(0,0).locale do {
      tileMatrix(0, 0).initializeData();
      tileMatrix(0, 0).setBorder();
  }

  const startTimeBoun = getCurrentTime();
  [i in {1..tmHeight}] { on tileMatrix(i, 0).locale do { tileMatrix(i, 0).initializeData(); } }
  [j in {1..tmWidth}]  { on tileMatrix(0, j).locale do { tileMatrix(0, j).initializeData(); } }
  const endTimeBoun = getCurrentTime();
  writeln("Main: initialized boundaries in ", (endTimeBoun - startTimeBoun), " seconds."); stdout.flush();

  const startTimeRow = getCurrentTime();
  [(i,j) in {1..tmWidth, 0..#tileWidth}] on tileMatrix(0, i).locale do {
    tileMatrix(0, i).bottomRow(j) = -1 * ((i - 1) * tileWidth + j + 1);
  }
  const endTimeRow = getCurrentTime();
  writeln("Main: initialized rows in ", (endTimeRow - startTimeRow), " seconds."); stdout.flush();

  const startTimeCol = getCurrentTime();
  [(i,j) in {1..tmHeight, 0..#tileHeight}]
    on tileMatrix(i, 0).locale do { tileMatrix(i, 0).rightColumn(j) = -1 * ((i - 1) * tileHeight + j + 1); }
  const endTimeCol = getCurrentTime();
  writeln("Main: initialized columns in ", (endTimeCol - startTimeCol), " seconds."); stdout.flush();

  const startTimeAvail = getCurrentTime();
  [i in {1..tmHeight}] { on tileMatrix(i, 0).locale do { tileMatrix(i, 0).setBorder(); } }
  [j in {1..tmWidth}]  { on tileMatrix(0, j).locale do { tileMatrix(0, j).setBorder(); } }
  const endTimeAvail = getCurrentTime();
  writeln("Main: boundary puts in ", (endTimeAvail - startTimeAvail), " seconds."); stdout.flush();

  writeln("Main: starting computation..."); stdout.flush();
  const startTimeComp = getCurrentTime();

  const tm_1_2d_domain = {1..tmHeight, 1..tmWidth};
  const tile_0_2d_domain = {0..tileHeight, 0..tileWidth};
  const tile_1_2d_domain = {1..tileHeight, 1..tileWidth};
  const tileHeight_1_domain = {1..tileHeight};
  const tileWidth_1_domain = {1..tileWidth};

  sync {

    for (i, j) in tm_1_2d_domain do {

      var i1 = i;
      var j1 = j;

      var left  = tileMatrix(i1 - 0, j1 - 1);
      var above = tileMatrix(i1 - 1, j1 - 0);
      var diag  = tileMatrix(i1 - 1, j1 - 1);

      on tileMatrix(i1, j1).locale do {
        var f1 = begin : bool {
          // wait till dependences are computed
          var hereId = here.id;

          // ensure left, above and diagonal have been computed
          left.get();
          above.get();
          diag.get();

          // perform computation
          var localMatrix: [tile_0_2d_domain] int;

          localMatrix(0, 0) = diag.diagonal;
          [i2 in tileHeight_1_domain] localMatrix(i2, 0)  = left.rightColumn(i2 - 1);
          [j2 in tileWidth_1_domain]  localMatrix(0 , j2) =  above.bottomRow(j2 - 1);

          var aDistrLoc = aDistr(hereId);
          var bDistrLoc = bDistr(hereId);
          for (ii, jj) in tile_1_2d_domain do {
	        var aIndex = ((j1 - 1) * tileWidth) + jj;
            var bIndex = ((i1 - 1) * tileHeight) + ii;

            var aElement = aDistrLoc(aIndex);
            var bElement = bDistrLoc(bIndex);

            var diagScore = localMatrix(ii - 1, jj - 1) + alignmentScoreMatrix(bElement, aElement);
            var leftScore = localMatrix(ii - 0, jj - 1) + alignmentScoreMatrix(aElement, 0);
            var topScore  = localMatrix(ii - 1, jj - 0) + alignmentScoreMatrix(0,        bElement);

            localMatrix(ii, jj) = max(diagScore, leftScore, topScore);
          } // end of for

          tileMatrix(i1, j1).initializeData();

          [idx in tileHeight_1_domain] tileMatrix(i1, j1).rightColumn(idx - 1) = localMatrix(idx   , tileWidth);
          [idx in tileWidth_1_domain]  tileMatrix(i1, j1).bottomRow(idx - 1)   = localMatrix(tileHeight, idx  );

          var res = true;
          res;
        };
        tileMatrix(i1, j1).f = f1;
      }
    }
  } // end of sync
  const execTimeComp = getCurrentTime() - startTimeComp;

  var score = tileMatrix(tmHeight, tmWidth).bottomRow(tileWidth-1);
  writeln("Main: The score is ", score);
  writeln("Main: Execution time ", execTimeComp, " seconds.");

  writeln("Main: ends.");
}
