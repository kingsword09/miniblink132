const IntlCollator = process._linkedBinding('electron_common_intl_collator').IntlCollator;

function Collator(langArray, resolvedOptions) {
	this.m_resolvedOptions = resolvedOptions;
	this.intl = new IntlCollator(langArray, resolvedOptions);
}

Collator.prototype.resolvedOptions = function () {
	return this.m_resolvedOptions;
}

Collator.prototype.compare = function(a, b) {
	return this.intl.compare(a, b);
}

exports.Collator = Collator;
