// A host's resource provider module (plan P3-21, createEngine({providers})):
// every image box is 150×50, whatever its src.
export default {
  resolve: (rows) => rows.map((r) => ({ resId: r.resId, w: 150, h: 50, baseline: 50 })),
};
