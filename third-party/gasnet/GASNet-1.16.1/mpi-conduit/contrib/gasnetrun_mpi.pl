#!/usr/bin/env perl
#   $Source: /var/local/cvs/gasnet/mpi-conduit/contrib/gasnetrun_mpi.pl,v $
#     $Date: 2010/12/05 22:21:11 $
# $Revision: 1.87.2.1 $
# Description: GASNet MPI spawner
# Terms of use are as specified in license.txt

require 5.004;
use strict;

# NOTE: The value of $ENV{'MPIRUN_CMD'} may be set in the shell wrapper
my $orig_spawncmd = $ENV{'MPIRUN_CMD'} || 'mpirun -np %N %P %A';
$orig_spawncmd = stripouterquotes($orig_spawncmd);
(my $spawncmd = $orig_spawncmd) =~ s/%C/%P %A/;	# deal with common alias

# Validate the spawncmd
my $cmd_ok = exists($ENV{'MPIRUN_CMD_OK'});
if ($spawncmd =~ m/MPIRUN_CMD_OK/) {
  $spawncmd =~ s/\s*MPIRUN_CMD_OK//g;
  $cmd_ok = 1;
}
unless ($cmd_ok ||
        (($spawncmd =~ m/%P/) && ($spawncmd =~ m/%[AQ]/) && ($spawncmd =~ m/%N/))) {
	die("gasnetrun: ERROR: MPIRUN_CMD='$orig_spawncmd'\n"
          . "The environment variable MPIRUN_CMD must contain the strings '%P' and '%A'\n"
          . "for expansion into the program and its arguments; and '%N' for expansion\n"
          . "into the number of processes ('%C' is acceptible as an alias for '%P %A'\n"
          . "and '%Q' may be substituted for '%A' to request extra quoting.)\n"
          . "To disable this check, set MPIRUN_CMD_OK in your environment, \n"
          . "or append the string MPIRUN_CMD_OK to the command.\n");
}

# Globals
my $envlist = '';
my $numproc = undef;
my $numnode = undef;
my $numcpu = undef; # For spawners that pin to a group of cpus.  0 means disable any such pinning.
my @numprocargs = ();
my $verbose = 0;
my @verbose_opt = ("-v");
my $keep = 0;
my $dryrun = 0;
my $exename = undef;
my $uname = `uname -a`;
my $find_exe = 1;	# should we find full path of executable?
my $env_before_exe = 1; # place env cmd before exe?
my $extra_quote_argv = 0; # add extra quotes around each argument
my $encode_args = 0; # encode command-line options to workaround buggy spawners
my $encode_env = 0;  # encode environment variables to workaround buggy spawners
my $group_join_argv = 0; # join all the args into one for %A?
my $force_nonempty_argv = 0; # if args are empty, still pass empty arg for %A
my $dashN_ok = 0; # does spawner support -N?
my $tmpdir = undef;
my $nodefile = $ENV{'GASNET_NODEFILE'} || $ENV{'PBS_NODEFILE'} ||
	($ENV{'PE_HOSTFILE'} && $ENV{'TMPDIR'} && -f "$ENV{'TMPDIR'}/machines" && "$ENV{'TMPDIR'}/machines") ||
	($ENV{'PE_HOSTFILE'} && $ENV{'TMP'} && -f "$ENV{'TMP'}/machines" && "$ENV{'TMP'}/machines");
my @tmpfiles = (defined($nodefile) && $ENV{'GASNET_RM_NODEFILE'}) ? ("$nodefile") : ();


################################################################################
## Encode args and env for safe consumption by GASNet
################################################################################

sub gasnet_encode($) {
    my ($in) = @_;
    my $permitted_chars = 'A-Za-z0-9%_,\\./:+=@^-';
    # don't encode unless we see special chars, to avoid negative interactions with higher-level encoding like upcrun
    return $in unless ($in =~ m/[^$permitted_chars]/);
    $in =~ s/%(0[0-9A-Fa-f][0-9A-Fa-f])/%025$1/g; # prevent false decodes
    $in =~ s/([^$permitted_chars])/sprintf("%%0%02x",(ord($1)))/ge;
    return $in;
}

# Define how to pass the environment vars
# 5 parameters to set: val, pre, inter, post and join
# To pass env as "-X A -Y B -Y C -Z" (a made up example)
#%envfmt = ('pre' => '-X', 'inter' => '-Y', 'post' => '-Z');
    my %envfmt = ();

# Probe for which MPI is running
# mpi-conduit/contrib/mpirun_h contains a database of recognized mpirun -h output strings
# if you add a line below, please also add an entry to that directory
    my $mpirun_cmd  = $spawncmd;
       $mpirun_cmd  =~ s/\s-.*/ -h/; # poe hangs on -help, so use -h
       $mpirun_cmd  =~ s/\s%[A-Za-z]+//g; # required for Cray MPI
    #print "probing: $mpirun_cmd\n";
    my $mpirun_help = `$mpirun_cmd 2>&1`;
    #print "probe result: $mpirun_help\n";
    my $is_lam      = ($mpirun_help =~ m|LAM/MPI|);
    my $is_ompi     = ($mpirun_help =~ m|OpenRTE|);
    my $is_mpiexec  = ($mpirun_help =~ m|mpiexec|);
    my $is_mpiexec_nt = ($mpirun_help =~ m|mpiexec| && $uname =~ m|cygwin|i );
    my $is_mpich_nt = ($mpirun_help =~ m|Unknown option| && $uname =~ m|cygwin|i );
    my $is_mpich    = ($mpirun_help =~ m|ch_p4|);
    my $is_mvich    = ($mpirun_help =~ m|MV(AP)?ICH|i);
    my $is_cray_mpi = ($mpirun_help =~ m|Psched|);
    my $is_crayt3e_mpi = ($uname =~ m|cray t3e|i );
    my $is_sgi_mpi = ($mpirun_help =~ m|\[-miser\]|);
    my $is_poe      = ($mpirun_help =~ m|Parallel Operating Environment|);
    my $is_aprun    = ($mpirun_help =~ m|aprunwrapper\|rchitecture type.*?xt|);
    my $is_yod      = ($mpirun_help =~ m| yod |);
    my $is_bgl_mpi  = ($mpirun_help =~ m|COprocessor or VirtualNode mode|);
    my $is_bgl_cqsub = ($mpirun_help =~ m| cqsub .*?co/vn|s);
#   my $is_bgp_mpi  = ($mpirun_help =~ m|fake-mpirun| && $mpirun_help =~ m|-partition|);
    my $is_bgp = ($mpirun_help =~ m|--mode <mode co/vn>|s);
    my $is_hp_mpi  = ($mpirun_help =~ m|-universe_size|);
    my $is_elan_mpi  = ($mpirun_help =~ m|MPIRUN_ELANIDMAP_FILE|);
    my $is_jacquard = ($mpirun_help =~ m| \[-noenv\] |) && !$is_elan_mpi;
    my $is_infinipath = ($mpirun_help =~ m|InfiniPath |);
    my $is_srun    = ($mpirun_help =~ m|srun: invalid option|);
    my $is_prun    = ($mpirun_help =~ m|railmask|);
    my $is_pam     = ($mpirun_help =~ m|TaskStarter|);
    my $envprog = $ENV{'ENVCMD'};
    if (! -x $envprog) { # SuperUX has broken "which" implementation, so avoid if possible
      $envprog = `which env`;
      chomp $envprog;
    }
    my $spawner_desc = undef;

    if ($ENV{'MPIRUN_CMD_BATCH'}) {
      print "WARNING: MPIRUN_CMD_BATCH only has significance on the BlueGene/P" unless($is_bgp);
    }

    if ($is_lam) {
	$spawner_desc = "LAM/MPI";
	# pass env as "-x A,B,C"
	%envfmt = ( 'pre' => '-x',
		    'join' => ','
		  );
	$dashN_ok = 1;
    } elsif ($is_ompi) {
	$spawner_desc = "OpenMPI";
	# pass env as "-x A -x B -x C"
	%envfmt = ( 'pre' => '-x',
		    'inter' => '-x'
		  );
    } elsif ($is_mpiexec) {
	$spawner_desc = "mpiexec/NT";
	# handles env for us
	%envfmt = ( 'noenv' => 1 );
    } elsif ($is_mpiexec) {
	$spawner_desc = "mpiexec";
	# handles env for us
	%envfmt = ( 'noenv' => 1 
		  );
	# mpiexec seems to brokenly insist on splitting argv on spaces, regardless of quoting
        # not much we can do about it...
    } elsif ($is_mpich_nt) {
	$spawner_desc = "MPICH/NT";
	# pass env as "-env A=1|B=2|C=3"
	%envfmt = ( 'pre' => '-env',
		    'join' => '|',
		    'val' => ''
		  );
	$find_exe = 0;
        $extra_quote_argv = 1;
    } elsif ($is_mvich) {
	$spawner_desc = "MVICH/MVAPICH";
	# pass env as "/usr/bin/env 'A=1' 'B=2' 'C=3'"
	%envfmt = ( 'pre' => $envprog,
		    'val' => "'"
		  );
        $extra_quote_argv = 1;
    } elsif ($is_mpich) {
	$spawner_desc = "MPICH";
	# pass env as "/usr/bin/env 'A=1' 'B=2' 'C=3'"
	%envfmt = ( 'pre' => $envprog,
		    'val' => "'"
		  );
    } elsif ($is_hp_mpi) {
	$spawner_desc = "HP MPI";
	# HP mpirun is a wrapper around many different backend-specific spawners, all with different behavior
	# Use a safe default
	%envfmt = ( 'pre' => $envprog,
		    'val' => ''
		  );
    } elsif ($is_elan_mpi) {
	$spawner_desc = "Quadrics/ELAN MPI";
	# this spawner already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1 );
	# unfortunately, it also botches spaces in arguments in an unrecoverable way
    } elsif ($is_cray_mpi) {
	$spawner_desc = "Cray MPI";
	# cannot reliably use /usr/bin/env at all when running via aprun 
        # (the binary doesnt support placed execution)
	# however, the OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1
                  );
    } elsif ($is_crayt3e_mpi) {
	$spawner_desc = "Cray T3E MPI";
	# OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1);
    } elsif ($is_sgi_mpi) {
	$spawner_desc = "SGI MPI";
	# OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1 );
	# Older spawner botches the argv quoting - BUT NOT RECENTLY
	# Use MPIRUN_CMD='mpirun -np %N %P %Q' if still a problem
    } elsif ($is_poe) {
	$spawner_desc = "IBM POE";
	# the OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1
                  );
        $extra_quote_argv = 1;
    } elsif ($is_aprun) {
	$spawner_desc = "Cray aprun";
	# the OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1
                  );
    } elsif ($is_yod) {
	$spawner_desc = "Catamount yod";
	# the OS already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1
                  );
	# what a mess: pbsyod needs extra quoting, bare yod does not.
	# recommend pbsyod users use "%Q" in MPIRUN_CMD
	@verbose_opt = ("-setenv", "PMI_DEBUG=1");
    } elsif ($is_bgl_mpi) {
	$spawner_desc = "IBM BG/L MPI";
	# pass as: -exp_env A -exp_env B
	%envfmt = ( 'pre' => '-exp_env',
		    'inter' => '-exp_env'
		  );
	$force_nonempty_argv = 1;
	$group_join_argv = 1;
	$env_before_exe = 1;
	@verbose_opt = ("-verbose", "2");
    } elsif ($is_bgl_cqsub) {
	$spawner_desc = "IBM BG/L cqsub";
	# pass as: -e A=val:B=val
	%envfmt = ( 'pre' => '-e',
		    'join' => ':',
		    'val' => ''
		  );
        $encode_env = 1; # botches spaces in environment values
        $encode_args = 1; # and in arguments
    } elsif ($is_bgp) {
        $spawner_desc = "IBM BG/P";
        if($ENV{'COBALT_JOBID'}) {
           %envfmt = ( 'pre' => '-env',
                       'inter' => '-env',
                       'val' => ''
                     );
        } else {
           %envfmt = ( 'pre' => '--env',
                       'join' => ':',
                       'val' => ''
                     );
        }   
        $encode_env = 1; # botches spaces in environment values
        $encode_args = 1; # and in arguments
        @verbose_opt = ("-verbose", "2");
    } elsif ($is_jacquard) {
	$spawner_desc = "NERSC/Jacquard mpirun";
	if (`hostname` =~ m/jaccn/) {
	  # compute node: pass env as "/usr/bin/env 'A=1' 'B=2' 'C=3'"
	  %envfmt = ( 'pre' => $envprog,
		      'val' => "'"
		    );
          $extra_quote_argv = 1;
	} else {
	  # front-end node: pass env as [/usr/bin/env '"A=1"' '"B=2"' '"C=3"'] to allow for extra shell
	  %envfmt = ( 'pre' => $envprog,
		      'lquote' => "'\"",
		      'rquote' => "\"'"
		    );
          $extra_quote_argv = 2;
	}
    } elsif ($is_infinipath) {
	$spawner_desc = "InfiniPath";
	%envfmt = ( 'pre' => $envprog, 'val' => '');
        $encode_args = 1;
        $encode_env = 1;
	@verbose_opt = ("-V");
    } elsif ($is_srun) {
	$spawner_desc = "SLURM srun";
	# this spawner already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1 );
	@verbose_opt = ("-v");
    } elsif ($is_prun) {
	$spawner_desc = "Quadrics/RMS prun";
	# this spawner already propagates the environment for us automatically
	%envfmt = ( 'noenv' => 1 );
	@verbose_opt = ("-v");
    } elsif ($is_pam) {
	$spawner_desc = "LSF pam";
	%envfmt = ( 'pre' => $envprog, 'val' => '');
	$encode_args = 1;
	$encode_env = 1;
	@verbose_opt = ("-v");
    } else {
	$spawner_desc = "unknown program (using generic MPI spawner)";
	# assume the OS will not propagate the environment
	# pass env as "/usr/bin/env A=1 B=2 C=3"
	# Our nearly universal default
	%envfmt = ( 'pre' => $envprog,
		    'val' => ''
		  );
        # use encoding as a safe default
        $encode_args = 1;
        $encode_env = 1;
    }

sub usage
{
    print (@_) if (@_);

    print "usage: gasnetrun -n <n> [options] [--] prog [program args]\n";
    print "    options:\n";
    print "      -n <n>                number of processes to run\n";
    print "      -N <n>                number of nodes to run on (not suppored on all mpiruns)\n";
    print "      -c <n>                number of cpus per process (not suppored on all mpiruns)\n";
    print "      -E <VAR1[,VAR2...]>   list of environment vars to propagate\n";
    print "      -v                    be verbose about what is happening\n";
    print "      -t                    test only, don't execute anything (implies -v)\n";
    print "      -k                    keep any temporary files created (implies -v)\n";
    print "      -(no)encode[-args,-env]   use encoding of args, env or both to help with buggy spawners\n";
    print "      --                    ends option parsing\n";
    exit 1;
}

sub stripouterquotes {
    my ($val) = @_;
    while ( $val =~ s/['"](.*?)['"]/$1/ ) { }
    return $val;
}

# "Multiply" array(s) for mapping procs to nodes
sub expand {
  my $ppn = int($numproc / $numnode);
  my $full = $numproc - $numnode * $ppn;  # nodes carrying ($ppn + 1) procs
  my $part = $numnode - $full;       # nodes carrying $ppn procs
                                                                                                              
  while (my $arr_ref = shift @_) {
    my @tmp = ();
    for (my $i = 0; $i < $full; ++$i) {
      my $elem = shift @$arr_ref;
      for (my $j = 0; $j <= $ppn; ++$j) { push @tmp, $elem; }
    }
    for (my $i = 0; $i < $part; ++$i) {
      my $elem = shift @$arr_ref;
      for (my $j = 0; $j < $ppn; ++$j) { push @tmp, $elem; }
    }
    @$arr_ref = @tmp;
  }
}

# We need to parse our command-line arguments
    while (@ARGV > 0) {
	$_ = $ARGV[0];

	if ($_ eq '--') {
	    shift;
	    last;
	} elsif ($_ eq '-n' || $_ eq '-np') {
	    shift;
	    usage ("$_ option given without an argument\n") unless @ARGV >= 1;
	    $numproc = 0+$ARGV[0];
	    usage ("$_ option with invalid argument '$ARGV[0]'\n") unless $numproc >= 1;
	} elsif ($_ =~ /^(-np?)([0-9]+)$/) {
	    $numproc = 0+$2;
	    usage ("$1 option with invalid argument '$2'\n") unless $numproc >= 1;
	} elsif ($_ eq '-N') {
	    shift;
	    usage ("$_ option given without an argument\n") unless @ARGV >= 1;
	    $numnode = 0+$ARGV[0];
	    usage ("$_ option with invalid argument '$ARGV[0]'\n") unless $numnode >= 1;
	} elsif ($_ =~ /^(-N)([0-9]+)$/) {
	    $numnode = 0+$2;
	    usage ("$1 option with invalid argument '$2'\n") unless $numnode >= 1;
	} elsif ($_ eq '-E') {
	    shift;
	    usage ("-E option given without an argument\n") unless @ARGV >= 1;
	    $envlist = $ARGV[0];
	} elsif ($_ eq '-c') {
	    shift;
	    usage ("$_ option given without an argument\n") unless @ARGV >= 1;
	    $numcpu = $ARGV[0];
	    usage ("$_ option given with invalid argument '$ARGV[0]'\n") unless $numcpu >= 0;
	} elsif ($_ eq '-v') {
	    $verbose = 1;
	} elsif ($_ eq '-t') {
	    $dryrun = 1;
	    $verbose = 1;
	} elsif ($_ eq '-k') {
	    $keep = 1;
	    $verbose = 1;
	} elsif ($_ =~ /^-+(no)?encode-args$/) {
          $encode_args = !(defined $1);
	} elsif ($_ =~ /^-+(no)?encode-env$/) {
          $encode_env = !(defined $1);
	} elsif ($_ =~ /^-+(no)?encode$/) {
          $encode_args = !(defined $1);
          $encode_env = !(defined $1);
	} elsif (m/^-/) {
	    usage ("unrecognized option '$_'\n");
	} else {
	    last;
	}
	shift;
    }

    print "gasnetrun: identified MPI spawner as: $spawner_desc\n" if ($verbose);

# Validate -n as needed
    if (!defined($numproc) && $spawncmd =~ /%N/) {
	usage "Required option -n was not given\n";
    }

# User's MPIRUN_CMD might request over-quoting explicitly
    $extra_quote_argv++ if ($spawncmd =~ s/%Q/%A/);

# Find the program
    my $exebase = shift or usage "No program specified\n";
    if ($find_exe) {
        if ($exebase =~ m|^/|) {
	    # full path, don't do anything to it
	    $exename = $exebase;
        } elsif ($exebase =~ m|/| || -x $exebase) {
	    # has directory components or exists in cwd
	    my $cwd = `pwd`;
	    chomp $cwd;
	    $exename = "$cwd/$exebase";
        } else {
	    # search PATH
	    foreach (split(':', $ENV{PATH})) {
	        my $tmp = "$_/$exebase";
	        if (-x $tmp) {
		    $exename = $tmp;
		    last;
	        }
	    }
        }
	if ($uname =~ m|cygwin|i) { # convert cygwin paths to windows paths for non-cygwin spawners
	  $exename = `cygpath -a -m $exename`;
	  chomp($exename);
	}
        die("gasnetrun: unable to locate program '$exebase'\n")
		    unless (defined($exename) && -x $exename);
        print("gasnetrun: located executable '$exename'\n") if ($verbose);
    } else {
        $exename = $exebase;
    }

# We need to gather a list of important environment variables
    # Form a list of the vars given by -E, plus any GASNET_* vars
    $ENV{"GASNET_VERBOSEENV"} = "1" if ($verbose);
    my @envvars = ((grep {+exists($ENV{$_})} split(',', $envlist)),
		   (grep {+m/^GASNET_/} keys(%ENV)));

# Build up the environment-passing arguments in several steps
    my @envargs = @envvars;
    if ($encode_env) {
      for my $var (@envvars) {
         my $val = $ENV{$var};
         $ENV{$var} = gasnet_encode($val);
         print "encoding ENV{$var}: '$val' => " . $ENV{$var} . "\n" if ($verbose && $val ne $ENV{$var});
      }
    }
    if (@envvars) {
        # pair the variables with their values if desired
        if (defined $envfmt{val}) {
	    my $q = $envfmt{val};
	    @envargs = map { "$_=$q$ENV{$_}$q" } @envargs;
        } elsif (defined $envfmt{lquote} || defined $envfmt{rquote}) {
	    my $lq = $envfmt{lquote};
	    my $rq = $envfmt{rquote};
	    @envargs = map { "$_=$lq$ENV{$_}$rq" } @envargs;
        }
        # join them into a single argument if desired
        if (defined $envfmt{join}) {
	    @envargs = join($envfmt{join}, @envargs);
        }
        # introduce 'inter' arg between variable (no effect if already joined)
        if (defined $envfmt{inter}) {
	    @envargs = map { ($_, $envfmt{inter}) } @envargs;
	    pop @envargs;
        }
        # tack on 'pre' and 'post' args
        if (defined $envfmt{pre}) {
	    unshift @envargs, $envfmt{pre};
        }
        if (defined $envfmt{post}) {
	    push @envargs, $envfmt{post};
        }
	if (defined $envfmt{noenv}) {
	    @envargs = ();
	}
    }
    print "envargs: " . (join " ", @envargs) . "\n" if ($verbose);

    # Special case for the mpich spawner
    if ($is_mpich && !$is_mpich_nt && !$is_mvich) {
        # General approach: create a wrapper script for the rsh/ssh command invoked by MPICH
        # that glues on the correct environment variables in a way that won't disturb MPICH
        $tmpdir = "gasnetrun_mpi-temp-$$";
        mkdir ($tmpdir, 0777) or die "gasnetrun: cannot create \'$tmpdir\'";
	my @spawners = ('ssh', 'rsh'); # default is to create ssh and rsh capture scripts
                                       # always create them because MPICH-GM device overrides RSHCOMMAND
        my $realprog = undef;
        my $realprog_args = undef;
        # If we have a direct path to the MPICH spawn script, rewrite it and replace RSHCOMMAND
        # for the most robust spawner capture (because RSHCOMMAND is sometimes an absolute path)
        if ($spawncmd =~ /^\s*(\S+)/ && -x "$1" && `grep 'RSHCOMMAND=' "$1" 2> /dev/null` ne "") {
          my $mpirun_script = stripouterquotes($1);
          my $tmprun = "$tmpdir/mpirun-tmp";
          my $tmprsh = 'mpirun-rsh';
          open (MPIRUN, $mpirun_script) or die "gasnetrun: can't open '$mpirun_script' for reading\n";
          open (TMPRUN, ">$tmprun") or die "gasnetrun: can't open '$tmprun' for writing\n";
          print "gasnetrun: cloning '$mpirun_script' to '$tmprun'\n" if ($verbose);
          while (<MPIRUN>) {
            my $line = $_;
            if ($line =~ /^\s*RSHCOMMAND=(.+)$/) {
              $realprog = $1;
              $line =~ s/$realprog/"$tmprsh"/; 
              $realprog = stripouterquotes($realprog);
              $realprog =~ s/^(\S+)\s*(.*)$/$1/;
              $realprog_args = $2;
              $realprog = stripouterquotes($realprog);
            }
            print TMPRUN "$line";
          }
          close (MPIRUN);
          close (TMPRUN);
	  chmod 0700, $tmprun or die "gasnetrun: cannot \'chmod 0700, $tmprun\'";
	  unshift @tmpfiles, "$tmprun";
	  if (!($realprog =~ /^\//)) { # RSHCOMMAND is a relative path - get absolute
             chomp($realprog = `which "$realprog" 2> /dev/null` || $realprog);
          }
          if (! -x "$realprog") {
	    print "gasnetrun: warning: cannot find MPICH underlying spawner '$realprog'\n" if ($verbose);
            $realprog = `which "ssh" 2> /dev/null`; $realprog_args = undef;
          }
	  unshift @spawners, $tmprsh;
          $spawncmd =~ s#$mpirun_script#$tmprun#;
        } 
	my $args = join(' ',map { "\"\'$_\'\"" } @envargs);
	(my $degooped_exename = $exename) =~ s/#/\\#/g;
	(my $degooped_args = join(' ',map { "\'$_\'" } @envargs)) =~ s/#/\\#/g;
	foreach my $spawner (@spawners) {
          unless (defined $realprog) { chomp($realprog = `which "$spawner" 2> /dev/null`); }
	  if (! -x "$realprog") { # Can't find that spawner - Assume we're not using it
            print "gasnetrun: warning: cannot find \'$spawner\'\n" if ($verbose);
	    next;
  	  }

	  my $tmpfile = "$tmpdir/$spawner";
	  unshift @tmpfiles, "$tmpfile";
          print "gasnetrun: building '$tmpfile' to wrap '$realprog'\n" if ($verbose);
	  open (TMPSPAWN, ">$tmpfile") or die "gasnetrun: cannot open $tmpfile";
	  print TMPSPAWN <<EOF;
#!/bin/sh

for arg in "\$@" ; do 
  #echo \$arg
  shift
  if test "\$arg" = "$exename" ; then 
    # prog name appears alone as an argument - prepend our env command
    set -- "\$@" $args "\'\$arg\'"
  elif test "\`echo \"\$arg\" | grep \"$exename\" ; exit 0\`"; then
    # prog name appears embedded in a larger quoted argument (eg mpich_gm/rsh)
    # keep it as one large arg and insert our env call
    newarg=`echo \"\$arg\" | sed \"s#$degooped_exename#$degooped_args \'$degooped_exename\'#\"`
    set -- "\$@" "\$newarg"
  else
    set -- "\$@" "\$arg"
  fi
done

if test "$verbose" != "0" ; then
  echo \$0 executing command:
  echo '$realprog' $realprog_args "\$@"
fi
  exec '$realprog' $realprog_args "\$@"
EOF
	  close(TMPSPAWN);
	  chmod 0700, $tmpfile or die "gasnetrun: cannot \'chmod 0700, $tmpfile\'";
          $realprog = undef;
          $realprog_args = undef;
   	}	
	$ENV{PATH} = "$tmpdir:$ENV{PATH}";
	@envargs = ();
     }

# Process LSF host list to ensure it conforms to our request
if (exists($ENV{'LSB_MCPU_HOSTS'})) {
  my @tmp = split(" ", $ENV{'LSB_MCPU_HOSTS'});
  my %tmp;
  while (@tmp) {
    my $h = shift @tmp; # Host
    my $n = shift @tmp; # Numcpus
    $tmp{$h} += $n;
  }
  # Ensure a dense sub-allocation
  my @hosts = (sort keys %tmp);
  my $np = 0;
  @tmp = ();
  if ($numnode) {
    die ("Not enough hosts LSB_MCPU_HOSTS to satisfy '-N $numnode'\n")
      unless (scalar(@hosts) >= $numnode);
    my $ppn = int($numproc / $numnode);    # quotient = minimum procs per node
    my $rem = $numproc - $numnode * $ppn;  # remainder = nodes carrying ($ppn + 1) procs
    foreach (@hosts) {
      my $extra = $rem?1:0;
      push @tmp, ($_, ($ppn + $extra));
      $rem -= $extra;
    }
  } else {
    foreach (@hosts) {
      my $n = $tmp{$_};
      if ($n > $numproc - $np) { $n = $numproc - $np; }
      push @tmp, ($_, $n);
      $np += $n;
      last if ($np == $numproc);
    }
    die ("Not enough hosts/cpus in LSB_MCPU_HOSTS to satisfy '-n $numproc'\n")
      unless ($np == $numproc);
  }
  $ENV{'LSB_MCPU_HOSTS'} = join(' ', @tmp);
  print("gasnetrun: rewrote LSB_MCPU_HOSTS='$ENV{'LSB_MCPU_HOSTS'}'\n") if ($verbose);
  $dashN_ok = 1;
}

# LAM-specific preprocessing of $numproc in the presence of $numnode
if ($is_lam && $numnode) {
  my @tmp = (0..($numnode-1));
  expand \@tmp;
  @numprocargs = ($numproc, 'n' . join(',', @tmp));
}
    
if (($is_srun || $is_prun) && $numnode) {
  @numprocargs = ($numproc, '-N', $numnode);
  $dashN_ok = 1;
}
    

if ($is_aprun || $is_yod) {
  @numprocargs = ($numproc);

  # Try to honor allocation if no -N given
  if (!defined($numnode)) {
        my $pbs_nodes = $ENV{'PBS_NNODES'};
        my $pbs_jobid = $ENV{'PBS_JOBID'};
        if (defined($pbs_nodes) && defined($ENV{'PBS_SIZE'})) {
            $numnode = $pbs_nodes;
        } elsif (defined($pbs_jobid) && open(QSTAT, "qstat -f $pbs_jobid |")) {
            while (<QSTAT>) {
                if (/mppnodect\s*=\s*([0-9]+)/) {
                    $numnode = $1;
                    last;
                }
            }
            close(QSTAT);
        }
  }

  if ($numnode) {
    my $ppn = int( ( $numproc + $numnode - 1 ) / $numnode );
    if ($is_aprun) { # aprun requires -N ppn
      push @numprocargs, ('-N', $ppn);
    } elsif ($ppn == 1) { # yod requires -SN or -VN
      push @numprocargs, '-SN';
    } elsif ($ppn == 2) {
      push @numprocargs, '-VN';
    } else {
      die "yod does not support more than 2 processes per node.\n";
    }
  }
  if ($is_aprun && defined($numcpu)) {
    push @numprocargs, ($numcpu ? ('-d', $numcpu) : qw/-cc none/);
  }
  $dashN_ok = 1;
}


if ($numproc && $is_bgp) {
  if(!defined($numnode)) {
    $numnode = $numproc
  }

  if ($ENV{'COBALT_JOBID'}) { # inside the job script
    print "inside cobalt job spawner\n" if ($verbose);
    #spawning command needs to be changed to cobalt-mpirun and not qsub
    $spawncmd = $ENV{'MPIRUN_CMD_BATCH'} || 'cobalt-mpirun %N %P %A';
    $spawncmd = stripouterquotes($spawncmd);
    $spawncmd =~ s/%C/%P %A/;  # deal with common alias
    $extra_quote_argv++ if ($spawncmd =~ s/%Q/%A/);
  
    my $ppn = int( ( $numproc + $numnode - 1 ) / $numnode );
    my $mode = $ENV{'GASNETRUN_MODE'};
    
    if ($mode) {
      # Assume the user's value is valid
    } elsif ($ppn == 1) {
      $mode = 'smp';
    } elsif ($ppn == 2) {
      $mode = 'dual';
    } elsif ($ppn == 4) {
      $mode = 'vn';
    } else {
      die "BG/P only supports 1, 2 or 4 ppn.  See README.dcmf.";
    }
    @numprocargs = ('-np', $numproc, '-mode', $mode);
  } else { # qsub requires
    my $ppn = int( ( $numproc + $numnode - 1 ) / $numnode );
    if ($ppn * $numnode != $numproc) {
    warn "WARNING: non-uniform process distribution not supported\n";
    warn "WARNING: PROCESS LAYOUT MIGHT NOT MATCH YOUR REQUEST\n";
    }
    if (my $mode = $ENV{'GASNETRUN_MODE'}) {
      if ($mode eq 'smp') {
        @numprocargs = ($numproc, '--mode', 'smp');
      } elsif ($mode eq 'dual') {
        @numprocargs = ($numproc/2, '--mode', 'dual');
      } elsif ($mode eq 'vn') {
        @numprocargs = ($numproc/4, '--mode', 'vn');
      } else {
        die "Invalid GASNETRUN_MODE=$mode";
      }
    } elsif ($ppn == 1) {
      @numprocargs = ($numproc, '--mode', 'smp');
    } elsif ($ppn == 2) {
      @numprocargs = ($numproc/2, '--mode', 'dual');
    } elsif ($ppn == 4) {
      @numprocargs = ($numproc/4, '--mode', 'vn');
    } else {
      die "BG/P only supports 1, 2 or 4 ppn";
    }
  }
  $dashN_ok = 1;
}

if ($is_bgp && $ENV{'COBALT_JOBID'}) {
  # Possibly deal with redirection by appending to @numprocargs
  @numprocargs = ($numproc) unless (@numprocargs); # default
  my $cwd = `pwd`;
  chomp $cwd;
  if (my $file = $ENV{'GASNETRUN_STDIN'}) {
    $file = "$cwd/$file" unless ($file =~ m,^/,);
    push @numprocargs, ('--stdin', $file);
  }
  if (my $file = $ENV{'GASNETRUN_STDOUT'}) {
    $file = "$cwd/$file" unless ($file =~ m,^/,);
    push @numprocargs, ('--stdout', $file);
  }
  if (my $file = $ENV{'GASNETRUN_STDERR'}) {
    $file = "$cwd/$file" unless ($file =~ m,^/,);
    push @numprocargs, ('--stderr', $file);
  }
}

if ($numnode && $is_infinipath) {
  my $ppn = int( ( $numproc + $numnode - 1 ) / $numnode );
  @numprocargs = ($numproc, '-ppn', $ppn);
  $dashN_ok = 1;
}

# Validate -N as needed
    if (defined($numnode) && !(($spawncmd =~ m/%M/) || $dashN_ok)) {
	warn "WARNING: Don't know how to control process->node layout with your mpirun\n";
	warn "WARNING: PROCESS LAYOUT MIGHT NOT MATCH YOUR REQUEST\n";
    }

# Exec it
    my $cwd = `pwd`;
    chomp $cwd;
    my @spawncmd = map {  if ($_ eq '%N') {
			      @numprocargs ? @numprocargs : $numproc;
			  } elsif ($_ eq '%M') {
			    $numnode || $numproc;
			  } elsif ($_ eq '%H') {
                              $nodefile or die "gasnetrun: %H appears in MPIRUN_CMD, but GASNET_NODEFILE is not set in the environment\n";
			  } elsif ($_ eq '%P') {
			      ( $env_before_exe ? (@envargs, $exename) : ($exename, @envargs) );
		              #my @tmp = @envargs;
			      #if ($env_before_exe) { push(@tmp, $exename); }
 			      #else { unshift(@tmp, $exename); }
			      #@tmp;
			  } elsif ($_ eq '%D') {
                              $cwd;
                          } elsif ($_ eq '%A') {
                              my @argv = @ARGV;
                              if ($encode_args) {
                                 @argv = map { $_ = gasnet_encode($_); } @argv;
			      }
			      @argv =    ( $extra_quote_argv == 1 ? (map { "'$_'" } @argv)
					 : $extra_quote_argv == 2 ? (map { "'\"$_\"'" } @argv)
					 : (@argv) );
			      ($force_nonempty_argv && !@argv ? ("") : 
                                ($group_join_argv ? join(' ', @argv) : @argv) );
                          } elsif ($_ eq '%V') {
			      $verbose?@verbose_opt:();
			  } else {
                              $_;
                          }
			} split(" ", $spawncmd);
    if ($verbose) { 
      my @quotedcmd = map {
           if (m/[ ]/ || m/^$/) {
	     "'$_'"; # quote tricky args for readability
           } else {
	     $_;
           } 
         } @spawncmd;
      print("gasnetrun: running: ", join(' ', @quotedcmd), "\n");
    }

    if ($dryrun) {
	# Do nothing
    } elsif (@tmpfiles || defined($tmpdir)) {
	system(@spawncmd);
	if (!$keep) {
          foreach (@tmpfiles) {
	    print("gasnetrun: unlinking ", join(' ', @tmpfiles), "\n") if ($verbose);
	    unlink "$_" or die "gasnetrun: failed to unlink \'$_\'";
	  }
	  if (defined($tmpdir)) {
	    rmdir $tmpdir or die "gasnetrun: failed to rmdir \'$tmpdir\'";
	  }
 	}
    } else {
	exec(@spawncmd);
	die "gasnetrun: exec(@spawncmd) failed: $!\n";
    }
    exit(0);
__END__