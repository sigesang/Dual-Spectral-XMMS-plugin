#!/usr/bin/perl

print "int gen_xscl[128]={ ";
for($i=0;$i<128;$i++) {
    print "\n        " if( $i !=0 && $i%16 == 0);
    print int(exp(($i+1)/128*log(256))-1);
    print", " if($i!=127);
    
}
print "};\n";
