for i in `find . -name 'Debug*'`
do
echo delete $i
rm -rf $i
rm -rf 'PTABen'
done
for i in `find . -name 'Release*'`
do
echo delete $i
rm -rf $i
rm -rf 'SVF-Tests'
done
