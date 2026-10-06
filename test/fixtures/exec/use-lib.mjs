// A module a document uses (#use, plan P3-31; D-I08): it registers with the
// $ of the execution that imports it — a fence, a math function — and keeps
// no state of its own (one instance serves every document of a worker).
export default function ($, std) {
  $.fence('shout', (body) => std.para(std.strong(std.text(body.trim().toUpperCase()))));
  $.math.fn('sq', ['x'], '#x^2');
}
