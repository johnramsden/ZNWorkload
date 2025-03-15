# Workloads

To generate workloads run:

```shell
cd vendor/workloadgen/core
mvn -Dtest=site.ycsb.generator.TestZipfianGeneratorZNS test
```

Workloads will be located in `target/workloads`.

Workloads can be tuned according to hardware availible by modifying paramaters in `core/src/test/java/site/ycsb/generator/TestZipfianGeneratorZNS.java`:

```java
final int zone_size = 1024 * 1024;
final int num_zones = 28;
final int iterations = 1500;
```