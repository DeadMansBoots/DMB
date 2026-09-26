# DMB: CI builds every branch but master with these properties, so they make DMB's own Android app,
# installed beside stock VCMI and VCMI's daily builds (package is.xyz.vcmi.dmb) in place of VCMI's
# daily app (is.xyz.vcmi.daily). Signed with the daily key committed in CI/android until DMB has a
# key and package name of its own.
dic = {
    "applicationIdSuffix": ".dmb",
    "applicationLabel": "Dead Man's Boots",
    "applicationVariant": "dmb",
    "signingConfig": "dailySigning",
}
print(";".join([f"{key}={value}" for key, value in dic.items()]))
